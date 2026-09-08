#pragma once

#include <LWS/Event.hpp>

#include <algorithm>
#include <cstdint>
#include <utility>
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
    class ListenerState final
    {
      public:

        explicit ListenerState(PlatformContext& platform) : platform_(platform) {}

        [[nodiscard]] uint64_t Add(EventCallback callback, bool beforeUserCallbacks = false)
        {
            const uint64_t id = nextId_++;
            auto listener = std::make_shared<Listener>(id, std::move(callback));
            if (beforeUserCallbacks)
                listeners_.insert(listeners_.begin(), std::move(listener));
            else
                listeners_.push_back(std::move(listener));
            return id;
        }

        void Remove(uint64_t id)
        {
            RemoveFrom(listeners_, id);
#ifdef LWS_PLATFORM_WIN32
            RemoveFrom(platformListeners_, id);
#endif
        }

        [[nodiscard]] bool Contains(uint64_t id) const
        {
            const bool portable = ContainsIn(listeners_, id);
#ifdef LWS_PLATFORM_WIN32
            return portable || ContainsIn(platformListeners_, id);
#else
            return portable;
#endif
        }

        [[nodiscard]] EventResponse Dispatch(const AnyEvent& event);

#ifdef LWS_PLATFORM_WIN32
        [[nodiscard]] uint64_t AddPlatform(Win32::PlatformCallback callback)
        {
            const uint64_t id = nextId_++;
            platformListeners_.push_back(std::make_shared<PlatformListener>(id, std::move(callback)));
            return id;
        }

        [[nodiscard]] bool DispatchPlatform(const Win32::PlatformEvent& event, LRESULT& result);
#endif

        void Close()
        {
            closed_ = true;
            CloseAll(listeners_);
#ifdef LWS_PLATFORM_WIN32
            CloseAll(platformListeners_);
#endif
        }

        [[nodiscard]] bool IsClosed() const { return closed_; }

      private:

        template<typename Callback>
        struct Registration
        {
            uint64_t id;
            Callback callback;
            bool connected{true};
        };

        template<typename List>
        static void RemoveFrom(List& listeners, uint64_t id)
        {
            std::erase_if(listeners,
                          [id](const auto& listener)
                          {
                              if (listener->id != id)
                                  return false;
                              listener->connected = false;
                              return true;
                          });
        }

        template<typename List>
        [[nodiscard]] static bool ContainsIn(const List& listeners, uint64_t id)
        {
            return std::ranges::any_of(listeners, [id](const auto& listener) { return listener->id == id; });
        }

        template<typename List>
        static void CloseAll(List& listeners)
        {
            for (const auto& listener : listeners)
                listener->connected = false;
            listeners.clear();
        }

        using Listener = Registration<EventCallback>;
#ifdef LWS_PLATFORM_WIN32
        using PlatformListener = Registration<Win32::PlatformCallback>;
#endif

        PlatformContext& platform_;
        std::vector<std::shared_ptr<Listener>> listeners_;
#ifdef LWS_PLATFORM_WIN32
        std::vector<std::shared_ptr<PlatformListener>> platformListeners_;
#endif
        uint64_t nextId_{1};
        bool closed_{};
    };
}  // namespace LWS::internal
