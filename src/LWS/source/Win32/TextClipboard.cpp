#ifdef LWS_PLATFORM_WIN32
    #include <LWS/TextClipboard.hpp>
    #include <windows.h>
    #include <LWS/Win32/WindowExtensions.hpp>
    #include <climits>
    #include "internal/ClipboardMemory.hpp"
    #include "../internal/WindowBackendAccess.hpp"
namespace LWS
{
    ClipboardResult SetClipboardText(Window& owner, std::string_view text)
    {
        if (!owner.IsCreated() || !owner.GetPlatformContext().IsUsable())
            return ClipboardResult::UnknownError;
        if (text.size() > INT_MAX || text.find('\0') != std::string_view::npos)
            return ClipboardResult::UnknownError;
        int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
        if (!text.empty() && !size)
            return ClipboardResult::UnknownError;
        std::wstring wide(size, L'\0');
        if (size && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), wide.data(),
                                        size) != size)
            return ClipboardResult::UnknownError;
        // Publish Unicode directly: the native clipboard synthesizes CF_TEXT when
        // requested, without routing UTF-8 through a locale-dependent conversion.
        Clipboard clipboard(owner.GetPlatformContext());
        return clipboard.SetClipboardData(owner, CF_UNICODETEXT, reinterpret_cast<const std::byte*>(wide.c_str()),
                                          (wide.size() + 1) * sizeof(wchar_t));
    }
    void RequestClipboardText(Window& owner, ClipboardTextCallback callback)
    {
        if (!callback)
            return;
        auto& context = owner.GetPlatformContext();
        const internal::WindowBackendAccess::DispatchScope windowDispatch(owner);
        const internal::PlatformContextAccess::DispatchScope contextDispatch(context);
        auto deliver = [&](ClipboardResult result, std::string text)
        {
            try
            {
                callback(result, std::move(text));
            }
            catch (...)
            {
                internal::PlatformContextAccess::ReportUnhandledException(context, std::current_exception());
            }
        };
        std::string text;
        auto result = ClipboardResult::UnknownError;
        {
            const auto handle = Win32::GetHwnd(owner);
            if (!handle)
            {
                deliver(ClipboardResult::UnknownError, {});
                return;
            }
            internal::ClipboardSession session(*handle);
            if (!session)
                result = ClipboardResult::AccessDenied;
            else
            {
                HANDLE data = GetClipboardData(CF_UNICODETEXT);
                if (data)
                {
                    auto lock = internal::GlobalMemory::LockBorrowed(data);
                    auto* wide = static_cast<const wchar_t*>(lock.data());
                    if (wide)
                    {
                        const size_t capacity = GlobalSize(data) / sizeof(wchar_t);
                        size_t length = 0;
                        while (length < capacity && wide[length])
                            ++length;
                        if (length < capacity && length <= INT_MAX)
                        {
                            int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, int(length), nullptr, 0,
                                                           nullptr, nullptr);
                            if (size || !length)
                            {
                                text.resize(size);
                                WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, int(length), text.data(), size,
                                                    nullptr, nullptr);
                                result = ClipboardResult::Success;
                            }
                        }
                    }
                }
            }
        }
        // Delayed clipboard rendering may synchronously destroy the requesting window.
        if (owner.IsCreated() && context.IsUsable())
            deliver(result, std::move(text));
    }
}  // namespace LWS
#endif
