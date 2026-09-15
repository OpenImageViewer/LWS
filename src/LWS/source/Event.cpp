#include <LWS/Event.hpp>

#include <LWS/Platform.hpp>

#include "internal/ListenerState.hpp"

#include <cassert>

namespace LWS
{
    EventConnection::EventConnection(std::weak_ptr<internal::ListenerState> state, uint64_t listenerId,
                                     std::thread::id threadId) noexcept
        : state_(std::move(state)), listenerId_(listenerId), threadId_(threadId), bound_(true)
    {
    }

    EventConnection::~EventConnection()
    {
        Disconnect();
    }

    EventConnection::EventConnection(EventConnection&& other) noexcept
    {
        MoveFrom(other);
    }

    EventConnection& EventConnection::operator=(EventConnection&& other) noexcept
    {
        if (this != &other)
        {
            assert(!bound_ || !other.bound_ || threadId_ == other.threadId_);
            Disconnect();
            MoveFrom(other);
        }
        return *this;
    }

    void EventConnection::Disconnect()
    {
        if (!bound_)
            return;

        assert(std::this_thread::get_id() == threadId_);
        if (const auto state = state_.lock(); state != nullptr)
            state->Remove(listenerId_);
        state_.reset();
        listenerId_ = 0;
    }

    bool EventConnection::IsConnected() const
    {
        if (!bound_)
            return false;

        assert(std::this_thread::get_id() == threadId_);
        const auto state = state_.lock();
        return state != nullptr && state->Contains(listenerId_);
    }

    void EventConnection::MoveFrom(EventConnection& other) noexcept
    {
        if (other.bound_)
            assert(std::this_thread::get_id() == other.threadId_);
        state_ = std::move(other.state_);
        listenerId_ = other.listenerId_;
        threadId_ = other.threadId_;
        bound_ = other.bound_;
        other.listenerId_ = 0;
        other.threadId_ = {};
        other.bound_ = false;
    }
}  // namespace LWS

namespace LWS::internal
{
    EventResponse ListenerState::Dispatch(const AnyEvent& event)
    {
        const bool destroying = std::holds_alternative<EventWindowDestroying>(event);
        const bool lifecycle = destroying || std::holds_alternative<EventWindowDestroyed>(event);
        // Ordinary nested events see the active list until the outermost dispatch returns. Teardown must also
        // reach newly registered dependents (including timers); Window rejects new registration while destroying,
        // so callbacks cannot structurally change the pending list being visited by lifecycle dispatch.
        const auto& listeners = lifecycle && pending_ ? *pending_ : listeners_;
        if (listeners.empty())
            return EventResponse::Unhandled;
        const DispatchScope scope(*this);
        for (const auto& listener : listeners)
        {
            if (!lifecycle && platform_.GetFailure().has_value())
                return EventResponse::Unhandled;
            if (listener->connected)
            {
                EventResponse response;
                try
                {
                    response = listener->callback(event);
                }
                catch (...)
                {
                    platform_.ReportUnhandledException(std::current_exception());
                    if (destroying)
                        continue;
                    return std::holds_alternative<EventCloseRequested>(event) ? EventResponse::Handled
                                                                              : EventResponse::Unhandled;
                }
                if (!destroying && response == EventResponse::Handled)
                    return response;
            }
        }
        return EventResponse::Unhandled;
    }

#ifdef LWS_PLATFORM_WIN32
    bool ListenerState::DispatchPlatform(const Win32::PlatformEvent& event, LRESULT& result)
    {
        if (platformListeners_.empty())
            return false;
        const DispatchScope scope(*this);
        const auto& listeners = platformListeners_;
        for (const auto& listener : listeners)
        {
            if (platform_.GetFailure().has_value())
                return false;
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
