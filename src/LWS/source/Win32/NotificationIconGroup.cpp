#ifdef LWS_PLATFORM_WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
    #include <Shellapi.h>
    #include <WindowsX.h>

    #include <LWS/NotificationIconGroup.hpp>
    #include <LWS/Win32/EventWin32.hpp>
    #include <LWS/Win32/WindowExtensions.hpp>
    #include <LWS/Window.hpp>
    #include "../internal/WindowBackendAccess.hpp"
    #include <LLUtils/Exception.h>
    #include <LLUtils/StringUtility.h>
    #include <LLUtils/Templates.h>
    #include <LLUtils/UniqueIDProvider.h>

    #include <set>

namespace LWS
{
    class NotificationIconGroup::Impl
    {
      public:

        explicit Impl(PlatformContext& platform) : fWindow(platform) {}

        ~Impl()
        {
            NOTIFYICONDATA nid{};
            nid.cbSize = sizeof(nid);
            nid.hWnd = reinterpret_cast<HWND>(internal::WindowBackendAccess::Get(fWindow)->getHandle());
            nid.uVersion = NOTIFYICON_VERSION_4;
            nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;

            for (const IconID id : fIconIDs)
            {
                nid.uID = id;
                Shell_NotifyIcon(NIM_DELETE, &nid);
            }
        }

        IconID AddIconResource(uint16_t iconResourceId, const string_type& tooltip,
                               NotificationIconEvent& notificationEvent)
        {
            if (!fWindow.IsCreated())
            {
                if (fWindow.Create({.visible = false}) != Result::Success)
                    LL_EXCEPTION(LLUtils::Exception::ErrorCode::InvalidState,
                                 "Unable to create the notification-icon window");
                const Result connection = Win32::SetPlatformCallback(
                    fWindow,
                    [this, &notificationEvent](const Win32::PlatformEvent& event)
                    {
                        if (const auto* notification = std::get_if<Win32::NotificationIconEvent>(&event))
                        {
                            HandleMessage(*notification, notificationEvent);
                            return std::optional<LRESULT>{0};
                        }
                        return std::optional<LRESULT>{};
                    });
                if (connection != Result::Success)
                {
                    LL_EXCEPTION(LLUtils::Exception::ErrorCode::InvalidState,
                                 "Unable to register the notification-icon platform callback");
                }
            }

            NOTIFYICONDATA nid{};
            nid.cbSize = sizeof(nid);
            nid.hWnd = reinterpret_cast<HWND>(internal::WindowBackendAccess::Get(fWindow)->getHandle());
            nid.uVersion = NOTIFYICON_VERSION_4;
            nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
            IconID iconId = fIconIdProvider.Acquire();
            nid.uID = static_cast<UINT>(iconId);
            nid.uCallbackMessage = Win32::NotificationIconEvent::MessageId;

            LLUtils::StringUtility::StrCpy(nid.szTip, tooltip.c_str(), LLUtils::array_length(nid.szTip));
            nid.hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(iconResourceId));

            if (Shell_NotifyIcon(NIM_ADD, &nid) == TRUE && Shell_NotifyIcon(NIM_SETVERSION, &nid) == TRUE)
            {
                fIconIDs.insert(iconId);
            }
            else
            {
                Shell_NotifyIcon(NIM_DELETE, &nid);
                fIconIdProvider.Release(iconId);
                LL_EXCEPTION_SYSTEM_ERROR("Cannot add notification icon");
            }

            return iconId;
        }

        Rect GetIconRect(IconID iconid) const
        {
            if (fIconIDs.contains(iconid))
            {
                NOTIFYICONIDENTIFIER iconIdentifer{static_cast<DWORD>(sizeof(NOTIFYICONIDENTIFIER)),
                                                   reinterpret_cast<HWND>(
                                                       internal::WindowBackendAccess::Get(fWindow)->getHandle()),
                                                   static_cast<UINT>(iconid), GUID{}};
                RECT rect{};

                if (Shell_NotifyIconGetRect(&iconIdentifer, &rect) == S_OK)
                {
                    return {{rect.left, rect.top}, {rect.right, rect.bottom}};
                }

                LL_EXCEPTION(LLUtils::Exception::ErrorCode::NotFound, "Icon id not found");
            }

            return {};
        }

      private:

        void HandleMessage(const Win32::NotificationIconEvent& message, NotificationIconEvent& notificationEvent)
        {
            switch (message.notification)
            {
                case NIN_SELECT:
                    notificationEvent.Raise(
                        NotificationIconEventArgs{NotificationIconAction::Select, message.x, message.y});
                    break;
                case WM_CONTEXTMENU:
                    notificationEvent.Raise(
                        NotificationIconEventArgs{NotificationIconAction::ContextMenu, message.x, message.y});
                    break;
                default:
                    break;
            }
        }

        std::set<IconID> fIconIDs;
        Window fWindow;
        LLUtils::UniqueIdProvider<IconID, std::set<IconID>> fIconIdProvider{1};
    };

    NotificationIconGroup::NotificationIconGroup(PlatformContext& platform)
        : platform_(platform), impl_(std::make_unique<Impl>(platform))
    {
        platform_.RegisterService();
    }

    NotificationIconGroup::~NotificationIconGroup()
    {
        platform_.AssertCurrentThread();
        impl_.reset();
        platform_.UnregisterService();
    }

    NotificationIconGroup::IconID NotificationIconGroup::AddIconResource(uint16_t iconResourceId,
                                                                         const string_type& tooltip)
    {
        platform_.AssertCurrentThread();
        return impl_->AddIconResource(iconResourceId, tooltip, OnNotificationIconEvent);
    }

    Rect NotificationIconGroup::GetIconRect(IconID iconid) const
    {
        platform_.AssertCurrentThread();
        return impl_->GetIconRect(iconid);
    }
}  // namespace LWS
#endif
