#ifdef LWS_PLATFORM_WAYLAND
    #include <LWS/TextClipboard.hpp>
    #include "internal/WaylandTextClipboard.hpp"
    #include "internal/PlatformState.hpp"
    #include <LWS/source/internal/WindowBackendAccess.hpp>
    #include <chrono>
    #include <cerrno>
    #include <fcntl.h>
    #include <memory>
    #include <signal.h>
    #include <unistd.h>
    #include <unordered_map>
    #include <utility>
    #include <vector>

namespace LWS::internal
{
    namespace
    {
        void DeliverClipboardText(Window& owner, ClipboardTextCallback& callback, ClipboardResult result,
                                  std::string text) noexcept
        {
            auto& context = owner.GetPlatformContext();
            const WindowBackendAccess::DispatchScope ownerDispatch(owner);
            const PlatformContextAccess::DispatchScope contextDispatch(context);
            try
            {
                callback(result, std::move(text));
            }
            catch (...)
            {
                PlatformContextAccess::ReportUnhandledException(context, std::current_exception());
            }
        }
        bool IsValidText(std::string_view text)
        {
            for (size_t index = 0; index < text.size();)
            {
                const auto first = static_cast<unsigned char>(text[index++]);
                if (first == 0)
                    return false;
                if (first < 0x80)
                    continue;
                uint32_t scalar = 0, minimum = 0;
                size_t remaining = 0;
                if ((first & 0xe0) == 0xc0)
                {
                    scalar = first & 0x1f;
                    minimum = 0x80;
                    remaining = 1;
                }
                else if ((first & 0xf0) == 0xe0)
                {
                    scalar = first & 0x0f;
                    minimum = 0x800;
                    remaining = 2;
                }
                else if ((first & 0xf8) == 0xf0)
                {
                    scalar = first & 0x07;
                    minimum = 0x10000;
                    remaining = 3;
                }
                else
                    return false;
                if (remaining > text.size() - index)
                    return false;
                for (size_t offset = 0; offset < remaining; ++offset)
                {
                    const auto next = static_cast<unsigned char>(text[index++]);
                    if ((next & 0xc0) != 0x80)
                        return false;
                    scalar = (scalar << 6) | (next & 0x3f);
                }
                if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
                    return false;
            }
            return true;
        }
        class FileDescriptor
        {
          public:

            explicit FileDescriptor(int fd = -1) noexcept : fd_(fd) {}
            ~FileDescriptor() { reset(); }
            FileDescriptor(const FileDescriptor&) = delete;
            FileDescriptor& operator=(const FileDescriptor&) = delete;
            FileDescriptor(FileDescriptor&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
            FileDescriptor& operator=(FileDescriptor&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    fd_ = std::exchange(other.fd_, -1);
                }
                return *this;
            }
            int get() const noexcept { return fd_; }
            void reset() noexcept
            {
                if (fd_ >= 0)
                    close(std::exchange(fd_, -1));
            }

          private:

            int fd_;
        };
        ssize_t PipeWrite(int fd, const char* bytes, size_t count)
        {
            sigset_t block, old, pending;
            sigemptyset(&block);
            sigaddset(&block, SIGPIPE);
            pthread_sigmask(SIG_BLOCK, &block, &old);
            sigpending(&pending);
            bool wasPending = sigismember(&pending, SIGPIPE);
            auto n = write(fd, bytes, count);
            int error = errno;
            if (n < 0 && error == EPIPE && !wasPending)
            {
                timespec timeout{};
                sigtimedwait(&block, nullptr, &timeout);
            }
            pthread_sigmask(SIG_SETMASK, &old, nullptr);
            errno = error;
            return n;
        }
    }  // namespace
    struct WaylandTextClipboard::State
    {
        explicit State(WaylandPlatformState& value) : platform(value) {}
        struct Source
        {
            Source() = default;
            Source(const Source&) = delete;
            Source& operator=(const Source&) = delete;
            ~Source()
            {
                if (handle)
                    wl_data_source_destroy(handle);
            }
            State* owner{};
            wl_data_source* handle{};
            std::string text;
        };
        struct Transfer
        {
            FileDescriptor fd;
            std::string text;
            size_t offset{};
        };
        struct Request
        {
            FileDescriptor fd;
            std::string text;
            ClipboardTextCallback callback;
            Window* owner{};
            State* state{};
            bool cancelled{};
            ClipboardResult failure = ClipboardResult::UnknownError;
            std::unique_ptr<wl_callback, decltype(&wl_callback_destroy)> synchronization{nullptr, wl_callback_destroy};
            EventConnection connection;
            std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        };
        WaylandPlatformState& platform;
        wl_data_device_manager* manager{};
        wl_data_device* device{};
        std::unique_ptr<wl_data_offer, decltype(&wl_data_offer_destroy)> selection{nullptr, wl_data_offer_destroy};
        bool utf8{}, plain{};
        std::unordered_map<wl_data_source*, std::unique_ptr<Source>> sources;
        std::vector<Transfer> sends;
        std::vector<std::unique_ptr<Request>> reads;
        static void synchronized(void* data, wl_callback*, uint32_t)
        {
            auto& request = *static_cast<Request*>(data);
            request.synchronization.reset();
            request.state->beginReceive(request);
        }
        void beginReceive(Request& request)
        {
            if (request.cancelled || !request.owner->IsCreated())
                return;
            if (!platform.hasKeyboardFocus())
            {
                request.failure = ClipboardResult::AccessDenied;
                return;
            }
            if (!selection || (!utf8 && !plain))
                return;
            int descriptors[2];
            if (pipe2(descriptors, O_CLOEXEC) != 0)
                return;
            FileDescriptor reader(descriptors[0]), writer(descriptors[1]);
            if (fcntl(reader.get(), F_SETFL, O_NONBLOCK) != 0)
                return;
            request.fd = std::move(reader);
            wl_data_offer_receive(selection.get(), utf8 ? "text/plain;charset=utf-8" : "text/plain", writer.get());
            writer.reset();
            wl_display_flush(platform.display());
        }
        static void target(void*, wl_data_source*, const char*) {}
        static void send(void* data, wl_data_source*, const char*, int32_t fd)
        {
            FileDescriptor descriptor(fd);
            auto& source = *static_cast<Source*>(data);
            const int flags = fcntl(fd, F_GETFL);
            if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
                return;
            source.owner->sends.push_back({std::move(descriptor), source.text, 0});
        }
        static void cancelled(void* data, wl_data_source* handle)
        {
            static_cast<Source*>(data)->owner->sources.erase(handle);
        }
        static void sourceDone(void*, wl_data_source*) {}
        static void sourceAction(void*, wl_data_source*, uint32_t) {}
        void process()
        {
            for (auto it = sends.begin(); it != sends.end();)
            {
                auto n = PipeWrite(it->fd.get(), it->text.data() + it->offset, it->text.size() - it->offset);
                if (n > 0)
                    it->offset += size_t(n);
                if (it->offset == it->text.size() || (n < 0 && errno != EAGAIN && errno != EINTR))
                {
                    it = sends.erase(it);
                }
                else
                    ++it;
            }
            // Complete callbacks after removing requests, so callbacks may enqueue more reads safely.
            std::vector<std::pair<std::unique_ptr<Request>, ClipboardResult>> completed;
            for (auto it = reads.begin(); it != reads.end();)
            {
                auto& r = **it;
                bool done = r.cancelled || !r.owner->IsCreated();
                auto result = r.failure;
                if (!done && !r.synchronization && r.fd.get() < 0)
                    done = true;
                if (!done && r.fd.get() >= 0)
                {
                    char buffer[4096];
                    // Match DnD fairness: large transfers yield to other ready UI work.
                    for (size_t received = 0; received < 64 * 1024;)
                    {
                        auto n = read(r.fd.get(), buffer, sizeof(buffer));
                        if (n > 0)
                        {
                            received += size_t(n);
                            r.text.append(buffer, size_t(n));
                            if (r.text.size() > 16 * 1024 * 1024)
                            {
                                done = true;
                                break;
                            }
                        }
                        else if (n == 0)
                        {
                            done = true;
                            result = IsValidText(r.text) ? ClipboardResult::Success : ClipboardResult::UnknownError;
                            break;
                        }
                        else if (errno == EINTR)
                            continue;
                        else
                        {
                            if (errno != EAGAIN)
                                done = true;
                            break;
                        }
                    }
                }
                if (std::chrono::steady_clock::now() - r.start >= std::chrono::seconds(5))
                    done = true;
                if (done)
                {
                    completed.emplace_back(std::move(*it), result);
                    completed.back().first->synchronization.reset();
                    completed.back().first->fd.reset();
                    if (result != ClipboardResult::Success)
                        completed.back().first->text.clear();
                    it = reads.erase(it);
                }
                else
                    ++it;
            }
            for (auto& [request, result] : completed)
            {
                if (!request->cancelled && platform.context().IsUsable() && request->owner->IsCreated())
                {
                    request->connection.Disconnect();
                    DeliverClipboardText(*request->owner, request->callback, result, std::move(request->text));
                }
            }
        }
    };

    WaylandTextClipboard::WaylandTextClipboard(WaylandPlatformState& platform)
        : state_(std::make_unique<State>(platform))
    {
    }
    WaylandTextClipboard::~WaylandTextClipboard() = default;
    void WaylandTextClipboard::setDevice(wl_data_device_manager* manager, wl_data_device* device)
    {
        reset();
        state_->manager = manager;
        state_->device = device;
    }
    void WaylandTextClipboard::setSelection(wl_data_offer* offer, bool utf8, bool plain)
    {
        state_->selection.reset(offer);
        state_->utf8 = utf8;
        state_->plain = plain;
    }
    void WaylandTextClipboard::appendPollDescriptors(std::vector<pollfd>& descriptors, int& timeoutMilliseconds) const
    {
        for (const auto& transfer : state_->sends)
            descriptors.push_back({.fd = transfer.fd.get(), .events = POLLOUT, .revents = 0});
        const auto now = std::chrono::steady_clock::now();
        for (const auto& request : state_->reads)
        {
            if (request->fd.get() >= 0)
                descriptors.push_back({.fd = request->fd.get(), .events = POLLIN, .revents = 0});
            // A failed synchronization/receive or owner cancellation needs completion now.
            const bool complete = request->cancelled || (!request->synchronization && request->fd.get() < 0);
            const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(request->start +
                                                                                std::chrono::seconds(5) - now);
            const int deadline = complete || remaining.count() <= 0 ? 0 : static_cast<int>(remaining.count());
            if (timeoutMilliseconds < 0 || timeoutMilliseconds > deadline)
                timeoutMilliseconds = deadline;
        }
    }
    void WaylandTextClipboard::process()
    {
        state_->process();
    }
    void WaylandTextClipboard::reset()
    {
        state_->manager = nullptr;
        state_->device = nullptr;
        state_->selection.reset();
        state_->sources.clear();
        state_->sends.clear();
        state_->utf8 = state_->plain = false;
        // Seat replacement fails outstanding reads on the next dispatch without invoking
        // user callbacks in the middle of protocol teardown. Owner destruction cancels them.
        for (auto& request : state_->reads)
        {
            request->synchronization.reset();
            request->fd.reset();
        }
    }
    ClipboardResult WaylandTextClipboard::setText(Window&, std::string_view text)
    {
        if (!IsValidText(text))
            return ClipboardResult::UnknownError;
        auto& state = *state_;
        if (!state.device || !state.platform.inputSerial())
            return ClipboardResult::AccessDenied;
        auto source = std::make_unique<State::Source>();
        source->text = text;
        source->owner = &state;
        source->handle = wl_data_device_manager_create_data_source(state.manager);
        if (!source->handle)
            return ClipboardResult::UnknownError;
        auto* handle = source->handle;
        auto entry = state.sources.emplace(handle, std::move(source)).first;
        static const wl_data_source_listener listener{State::target,     State::send,       State::cancelled,
                                                      State::sourceDone, State::sourceDone, State::sourceAction};
        wl_data_source_add_listener(handle, &listener, entry->second.get());
        wl_data_source_offer(handle, "text/plain;charset=utf-8");
        wl_data_source_offer(handle, "text/plain");
        wl_data_device_set_selection(state.device, handle, state.platform.inputSerial());
        wl_display_flush(state.platform.display());
        return ClipboardResult::Success;
    }
    void WaylandTextClipboard::requestText(Window& owner, ClipboardTextCallback callback)
    {
        auto& state = *state_;
        // Offers survive same-client surface switches, but may not be received while
        // this client lacks keyboard focus.
        if (!state.platform.hasKeyboardFocus())
        {
            DeliverClipboardText(owner, callback, ClipboardResult::AccessDenied, {});
            return;
        }
        auto request = std::make_unique<State::Request>();
        request->owner = &owner;
        request->state = &state;
        request->callback = std::move(callback);
        auto connection = owner.Listen(
            [pending = request.get()](const AnyEvent& event)
            {
                if (std::holds_alternative<EventWindowDestroying>(event) ||
                    std::holds_alternative<EventWindowDestroyed>(event))
                    pending->cancelled = true;
                return EventResponse::Unhandled;
            });
        if (!connection)
        {
            DeliverClipboardText(owner, request->callback, ClipboardResult::UnknownError, {});
            return;
        }
        request->connection = std::move(*connection);
        // Some compositors send selection after keyboard.enter. Wait for the focus
        // event batch before choosing an offer, including requests from FocusGained.
        request->synchronization.reset(wl_display_sync(state.platform.display()));
        if (!request->synchronization)
        {
            DeliverClipboardText(owner, request->callback, ClipboardResult::UnknownError, {});
            return;
        }
        static const wl_callback_listener listener{State::synchronized};
        wl_callback_add_listener(request->synchronization.get(), &listener, request.get());
        state.reads.push_back(std::move(request));
        wl_display_flush(state.platform.display());
    }
}  // namespace LWS::internal
namespace LWS
{
    ClipboardResult SetClipboardText(Window& owner, std::string_view text)
    {
        auto& context = owner.GetPlatformContext();
        if (!context.IsUsable() || !owner.IsCreated())
            return ClipboardResult::UnknownError;
        auto& platform = *static_cast<internal::WaylandPlatformState*>(
            internal::PlatformContextAccess::GetBackend(context));
        return platform.dataDevice().setClipboardText(owner, text);
    }
    void RequestClipboardText(Window& owner, ClipboardTextCallback callback)
    {
        auto& context = owner.GetPlatformContext();
        if (!callback)
            return;
        if (context.IsUsable() && owner.IsCreated())
        {
            auto& platform = *static_cast<internal::WaylandPlatformState*>(
                internal::PlatformContextAccess::GetBackend(context));
            platform.dataDevice().requestClipboardText(owner, std::move(callback));
            return;
        }
        internal::DeliverClipboardText(owner, callback, ClipboardResult::UnknownError, {});
    }
}  // namespace LWS
#endif
