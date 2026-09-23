#pragma once
#include <algorithm>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace LWSUI
{
    // UI-thread event: connections own registrations, the publisher observes them.
    // A dispatch snapshot permits subscription/removal without moving an executing callable.
    template <class Signature>
    class Event;
    template <class... Args>
    class Event<void(Args...)>
    {
        struct Listener
        {
            std::function<void(Args...)> callback;
            bool connected = true;
        };

      public:

        class Connection
        {
          public:

            Connection() = default;
            Connection(const Connection&) = delete;
            Connection& operator=(const Connection&) = delete;
            Connection(Connection&& other) noexcept = default;
            Connection& operator=(Connection&& other) noexcept
            {
                if (this != &other)
                {
                    Disconnect();
                    listener_ = std::move(other.listener_);
                }
                return *this;
            }
            ~Connection() { Disconnect(); }
            void Disconnect()
            {
                if (listener_)
                    listener_->connected = false;
                listener_.reset();
            }

          private:

            friend class Event;
            explicit Connection(std::shared_ptr<Listener> listener) : listener_(std::move(listener)) {}
            std::shared_ptr<Listener> listener_;
        };
        Event() = default;
        Event(const Event&) = delete;
        Event& operator=(const Event&) = delete;
        ~Event()
        {
            for (auto& weak : listeners_)
                if (auto listener = weak.lock())
                    listener->connected = false;
        }
        [[nodiscard]] Connection Connect(std::function<void(Args...)> callback)
        {
            std::erase_if(listeners_, [](const auto& weak) { return weak.expired(); });
            auto listener = std::make_shared<Listener>(std::move(callback));
            listeners_.push_back(listener);
            return Connection(std::move(listener));
        }
        void Raise(Args... args)
        {
            const auto snapshot = listeners_;
            for (const auto& weak : snapshot)
                if (auto listener = weak.lock(); listener && listener->connected)
                    listener->callback(args...);
        }

      private:

        std::vector<std::weak_ptr<Listener>> listeners_;
    };
}  // namespace LWSUI
