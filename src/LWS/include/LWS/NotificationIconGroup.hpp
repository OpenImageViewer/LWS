#pragma once

#include <LWS/Platform.hpp>
#include <LLUtils/Event.h>
#include <LLUtils/Rect.h>

#include <cstdint>
#include <memory>

namespace LWS
{
    class NotificationIconGroup final
    {
      public:

        using IconID = uint8_t;

        enum class NotificationIconAction
        {
            None,
            Select,
            ContextMenu,
        };

        struct NotificationIconEventArgs
        {
            NotificationIconAction action = NotificationIconAction::None;
            int16_t mouseX = 0;
            int16_t mouseY = 0;
        };

        using NotificationIconEvent = LLUtils::Event<void(NotificationIconEventArgs)>;

        IconID AddIconResource(uint16_t iconResourceId, const string_type& tooltip);
        ~NotificationIconGroup();

        NotificationIconGroup(const NotificationIconGroup&) = delete;
        NotificationIconGroup& operator=(const NotificationIconGroup&) = delete;
        NotificationIconGroup(NotificationIconGroup&&) noexcept = delete;
        NotificationIconGroup& operator=(NotificationIconGroup&&) noexcept = delete;

        explicit NotificationIconGroup(PlatformContext& platform);

        [[nodiscard]] Rect GetIconRect(IconID iconid) const;

        NotificationIconEvent OnNotificationIconEvent;

      private:

        class Impl;
        PlatformContext& platform_;
        std::unique_ptr<Impl> impl_;
    };
}  // namespace LWS
