#include <LWS/Event.hpp>

#include <LWS/Platform.hpp>
#include <LWS/Window.hpp>

#include "internal/ListenerState.hpp"

#include <cassert>

namespace LWS
{
    EventListenerGuard& EventListenerGuard::operator=(EventListenerGuard&& other) noexcept
    {
        if (this != &other)
        {
            if (window)
            {
                window->RemoveEventListener(token);
            }

            window = other.window;
            token = other.token;
            other.window = nullptr;
        }

        return *this;
    }

    EventListenerGuard::~EventListenerGuard()
    {
        if (window)
        {
            window->RemoveEventListener(token);
        }
    }
}

namespace LWS::internal
{
    EventResponse ListenerState::Dispatch(const AnyEvent& event)
    {
        // Retain the executing callable when a callback adds, removes, or closes registrations.
        const auto listeners = listeners_;
        for (const auto& listener : listeners)
        {
            if (listener->connected)
            {
                EventResponse response;
                try
                {
                    response = listener->callback(event) ? EventResponse::Handled : EventResponse::Unhandled;
                }
                catch (...)
                {
                    platform_.ReportUnhandledException(std::current_exception());
                    return std::holds_alternative<EventCloseRequested>(event) ? EventResponse::Handled
                                                                              : EventResponse::Unhandled;
                }
                if (response == EventResponse::Handled)
                    return response;
            }
        }
        return EventResponse::Unhandled;
    }

#ifdef LWS_PLATFORM_WIN32
    bool ListenerState::DispatchPlatform(const Win32::PlatformEvent& event, LRESULT& result)
    {
        const auto listeners = platformListeners_;
        for (const auto& listener : listeners)
        {
            if (!listener->connected)
                continue;
            try
            {
                if (const auto overrideResult = listener->callback(event); overrideResult.has_value())
                {
                    result = *overrideResult;
                    return true;
                }
            }
            catch (...)
            {
                platform_.ReportUnhandledException(std::current_exception());
                return false;
            }
        }
        return false;
    }
#endif
}  // namespace LWS::internal
