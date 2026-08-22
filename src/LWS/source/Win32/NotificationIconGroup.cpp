#ifdef LWS_PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Shellapi.h>
#include <WindowsX.h>

#include <LWS/NotificationIconGroup.hpp>
#include <LWS/Win32/EventWin32.hpp>
#include <LWS/Window.hpp>
#include <LLUtils/Exception.h>
#include <LLUtils/StringUtility.h>
#include <LLUtils/Templates.h>
#include <LLUtils/UniqueIDProvider.h>

#include <map>
#include <set>

namespace LWS
{
    class NotificationIconGroup::Impl
    {
    public:
        static constexpr UINT WM_PRIVATE_NOTIFICATION_CALLBACK_MESSAGE_ID = WM_USER + 1;

        Impl() = default;

        ~Impl()
        {
            NOTIFYICONDATA nid{};
            nid.cbSize = sizeof(nid);
            nid.hWnd = reinterpret_cast<HWND>(fWindow.GetHandle());
            nid.uVersion = NOTIFYICON_VERSION_4;
            nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;

            for (const auto& [id, _] : fMapIconData)
            {
                nid.uID = id;
                Shell_NotifyIcon(NIM_DELETE, &nid);
            }
        }

        IconID AddIconResource(uint16_t iconResourceId, const string_type& tooltip, NotificationIconEvent& notificationEvent)
        {
            if (fWindow.GetHandle() == 0)
            {
                std::ignore = fWindow.Create({ .visible = false });
                fWindow.SetVisible(false);
                fWindow.AddEventListener([this, &notificationEvent](const AnyEvent& eventData)
                {
                    return OnWindowMessage(eventData, notificationEvent);
                });
            }

            NOTIFYICONDATA nid{};
            nid.cbSize = sizeof(nid);
            nid.hWnd = reinterpret_cast<HWND>(fWindow.GetHandle());
            nid.uVersion = NOTIFYICON_VERSION_4;
            nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
            IconID iconId = fIconIdProvider.Acquire();
            nid.uID = static_cast<UINT>(iconId);
            nid.uCallbackMessage = WM_PRIVATE_NOTIFICATION_CALLBACK_MESSAGE_ID;

            LLUtils::StringUtility::StrCpy(nid.szTip, tooltip.c_str(), LLUtils::array_length(nid.szTip));
            nid.hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(iconResourceId));

            if (Shell_NotifyIcon(NIM_ADD, &nid) == TRUE && Shell_NotifyIcon(NIM_SETVERSION, &nid) == TRUE)
            {
                fMapIconData.emplace(iconId, NotificationIconData{ iconId });
            }
            else
            {
                fIconIdProvider.Release(iconId);
                LL_EXCEPTION_SYSTEM_ERROR("Cannot add notification icon");
            }

            return iconId;
        }

        LLUtils::Rect<uint16_t> GetIconRect(IconID iconid) const
        {
            auto iconData = fMapIconData.find(iconid);
            if (iconData != fMapIconData.end())
            {
                NOTIFYICONIDENTIFIER iconIdentifer{ static_cast<DWORD>(sizeof(NOTIFYICONIDENTIFIER)),
                                                   reinterpret_cast<HWND>(fWindow.GetHandle()),
                                                   static_cast<UINT>(iconid),
                                                   GUID{} };
                RECT rect{};

                if (Shell_NotifyIconGetRect(&iconIdentifer, &rect) == S_OK)
                {
                    using type = LLUtils::Rect<uint16_t>::Point_Type::point_type;
                    return { LLUtils::Rect<uint16_t>::Point_Type{ static_cast<type>(rect.left), static_cast<type>(rect.top) },
                             { static_cast<type>(rect.right), static_cast<type>(rect.bottom) } };
                }

                LL_EXCEPTION(LLUtils::Exception::ErrorCode::NotFound, "Icon id not found");
            }

            return {};
        }

    private:
        bool OnWindowMessage(const AnyEvent& eventData, NotificationIconEvent& notificationEvent)
        {
            const auto* raw = std::get_if<EventRawPlatform>(&eventData);
            if (raw == nullptr || raw->platformType != std::to_underlying(BackendId::Win32) || raw->platformData == nullptr)
            {
                return false;
            }

            const auto& message = *reinterpret_cast<const Win32::WinMessage*>(raw->platformData);
            if (message.message == WM_PRIVATE_NOTIFICATION_CALLBACK_MESSAGE_ID)
            {
                HandleMessage(message, notificationEvent);
                return true;
            }

            return false;
        }

        void HandleMessage(const Win32::WinMessage& message, NotificationIconEvent& notificationEvent)
        {
            UINT notificationMessage = LOWORD(message.lParam);
            int16_t x = static_cast<int16_t>(GET_X_LPARAM(message.wParam));
            int16_t y = static_cast<int16_t>(GET_Y_LPARAM(message.wParam));

            switch (notificationMessage)
            {
            case NIN_SELECT:
                notificationEvent.Raise(NotificationIconEventArgs{ NotificationIconAction::Select, x, y });
                break;
            case WM_CONTEXTMENU:
                notificationEvent.Raise(NotificationIconEventArgs{ NotificationIconAction::ContextMenu, x, y });
                break;
            default:
                break;
            }
        }

        struct NotificationIconData
        {
            IconID id;
        };

        std::map<IconID, NotificationIconData> fMapIconData;
        Window fWindow;
        LLUtils::UniqueIdProvider<IconID, std::set<IconID>> fIconIdProvider{ 1 };
    };

    NotificationIconGroup::NotificationIconGroup() : impl_(std::make_unique<Impl>()) {}
    NotificationIconGroup::~NotificationIconGroup() = default;

    NotificationIconGroup::IconID NotificationIconGroup::AddIconResource(uint16_t iconResourceId, const string_type& tooltip)
    {
        return impl_->AddIconResource(iconResourceId, tooltip, OnNotificationIconEvent);
    }

    LLUtils::Rect<uint16_t> NotificationIconGroup::GetIconRect(IconID iconid) const
    {
        return impl_->GetIconRect(iconid);
    }
}
#endif
