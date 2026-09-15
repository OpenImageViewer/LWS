#pragma once
#include <LWS/Event.hpp>
#include <algorithm>
#include <cstdint>
#include <utility>
#include <optional>
#include <vector>
#ifdef LWS_PLATFORM_WIN32
    #include <LWS/Win32/EventWin32.hpp>
#endif
namespace LWS
{
    class PlatformContext;
}
namespace LWS::internal
{
    // Ordinary dispatch borrows stable active vectors. Mutations prepare a pending vector once per outer dispatch;
    // publication swaps ownership without allocation. Shared registrations preserve immediate disconnection and captures.
    class ListenerState final
    {
        template <class Callback>
        struct Registration
        {
            uint64_t id;
            Callback callback;
            bool connected{true};
        };
        using Listener = Registration<EventCallback>;
        using List = std::vector<std::shared_ptr<Listener>>;
#ifdef LWS_PLATFORM_WIN32
        using PlatformListener = Registration<Win32::PlatformCallback>;
        using PlatformList = std::vector<std::shared_ptr<PlatformListener>>;
#endif

      public:

        explicit ListenerState(PlatformContext& platform) : platform_(platform) {}
        [[nodiscard]] uint64_t Add(EventCallback callback, bool beforeUserCallbacks = false)
        {
            if (closed_)
                return 0;
            const auto id = nextId_++;
            auto listener = std::make_shared<Listener>(id, std::move(callback));
            auto& list = Writable(listeners_, pending_);
            if (beforeUserCallbacks)
                list.insert(list.begin(), std::move(listener));
            else
                list.push_back(std::move(listener));
            return id;
        }
#ifdef LWS_PLATFORM_WIN32
        [[nodiscard]] uint64_t AddPlatform(Win32::PlatformCallback callback)
        {
            if (closed_)
                return 0;
            const auto id = nextId_++;
            auto listener = std::make_shared<PlatformListener>(id, std::move(callback));
            Writable(platformListeners_, pendingPlatform_).push_back(std::move(listener));
            return id;
        }
        [[nodiscard]] bool DispatchPlatform(const Win32::PlatformEvent& event, LRESULT& result);
#endif
        [[nodiscard]] EventResponse Dispatch(const AnyEvent& event);
        [[nodiscard]] bool Contains(uint64_t id) const
        {
            if (closed_)
                return false;
            bool found = ContainsIn(pending_ ? *pending_ : listeners_, id);
#ifdef LWS_PLATFORM_WIN32
            found = found || ContainsIn(pendingPlatform_ ? *pendingPlatform_ : platformListeners_, id);
#endif
            return found;
        }
        void Remove(uint64_t id)
        {
            dirty_ = Mark(pending_ ? *pending_ : listeners_, id) || dirty_;
#ifdef LWS_PLATFORM_WIN32
            dirty_ = Mark(pendingPlatform_ ? *pendingPlatform_ : platformListeners_, id) || dirty_;
#endif
            FlushIfIdle();
        }
        void Close()
        {
            if (closed_)
                return;
            closed_ = true;
            MarkAll(listeners_);
            if (pending_)
                MarkAll(*pending_);
#ifdef LWS_PLATFORM_WIN32
            MarkAll(platformListeners_);
            if (pendingPlatform_)
                MarkAll(*pendingPlatform_);
#endif
            dirty_ = true;
            FlushIfIdle();
        }
        [[nodiscard]] bool IsClosed() const { return closed_; }

      private:

        class DispatchScope
        {
          public:

            explicit DispatchScope(ListenerState& state) : state_(state) { ++state_.depth_; }
            ~DispatchScope()
            {
                --state_.depth_;
                if (state_.depth_ == 0 && state_.dirty_)
                    state_.FlushIfIdle();
            }
            DispatchScope(const DispatchScope&) = delete;
            DispatchScope& operator=(const DispatchScope&) = delete;

          private:

            ListenerState& state_;
        };
        template <class V>
        V& Writable(V& active, std::optional<V>& pending)
        {
            if (depth_ == 0 && !publishing_)
                return active;
            if (!pending)
            {
                // Complete the copy before installing it, so allocation failure cannot publish an empty list.
                V replacement;
                replacement.reserve(active.size() + 1);
                replacement.assign(active.begin(), active.end());
                pending.emplace(std::move(replacement));
            }
            dirty_ = true;
            return *pending;
        }
        template <class V>
        static bool ContainsIn(const V& list, uint64_t id)
        {
            return std::ranges::any_of(list, [=](const auto& v) { return v->id == id && v->connected; });
        }
        template <class V>
        static bool Mark(V& list, uint64_t id)
        {
            for (auto& v : list)
                if (v->id == id && v->connected)
                {
                    v->connected = false;
                    return true;
                }
            return false;
        }
        template <class V>
        static void MarkAll(V& list)
        {
            for (auto& v : list)
                v->connected = false;
        }
        template <class V>
        static void Compact(V& list)
        {
            // NOTE: Repeated erasure shifts remaining entries, making this O(N^2) in the worst case.
            size_t i = 0;
            while (i < list.size())
            {
                if (list[i]->connected)
                    ++i;
                else
                {
                    auto retired = std::move(list[i]);
                    list.erase(list.begin() + i);
                    // Release captures only after editing the vector. Reentrant changes are deferred during Flush.
                    retired.reset();
                }
            }
        }
        template <class V>
        static void Publish(V& active, std::optional<V>& pending, V& retired)
        {
            if (pending)
            {
                active.swap(*pending);
                retired = std::move(*pending);
                pending.reset();
            }
        }
        void FlushIfIdle() noexcept
        {
            if (depth_ != 0 || publishing_ || !dirty_)
                return;
            publishing_ = true;
            while (dirty_)
            {
                dirty_ = false;
                List retired;
                List retiredPending;
#ifdef LWS_PLATFORM_WIN32
                PlatformList retiredPlatform;
                PlatformList retiredPendingPlatform;
#endif
                if (closed_)
                {
                    // Detach both channels before releasing any captures. Close stays linear even for large lists,
                    // and capture destructors see empty current lists if they reenter connection operations.
                    retired.swap(listeners_);
                    if (pending_)
                    {
                        retiredPending.swap(*pending_);
                        pending_.reset();
                    }
#ifdef LWS_PLATFORM_WIN32
                    retiredPlatform.swap(platformListeners_);
                    if (pendingPlatform_)
                    {
                        retiredPendingPlatform.swap(*pendingPlatform_);
                        pendingPlatform_.reset();
                    }
#endif
                }
                else
                {
                    Publish(listeners_, pending_, retired);
#ifdef LWS_PLATFORM_WIN32
                    Publish(platformListeners_, pendingPlatform_, retiredPlatform);
#endif
                    Compact(listeners_);
#ifdef LWS_PLATFORM_WIN32
                    Compact(platformListeners_);
#endif
                }
                // Both lists are published before retired callback destruction can reenter this state.
            }
            publishing_ = false;
        }
        PlatformContext& platform_;
        List listeners_;
        std::optional<List> pending_;
#ifdef LWS_PLATFORM_WIN32
        PlatformList platformListeners_;
        std::optional<PlatformList> pendingPlatform_;
#endif
        uint64_t nextId_{1};
        unsigned depth_{};
        bool closed_{};
        bool dirty_{};
        bool publishing_{};
    };
}  // namespace LWS::internal
