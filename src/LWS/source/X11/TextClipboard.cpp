#ifdef LWS_PLATFORM_X11
    #include <LWS/TextClipboard.hpp>
namespace LWS
{
    ClipboardResult SetClipboardText(Window&, std::string_view)
    {
        return ClipboardResult::UnknownError;
    }
    void RequestClipboardText(Window&, ClipboardTextCallback callback)
    {
        callback(ClipboardResult::UnknownError, {});
    }
}  // namespace LWS
#endif
