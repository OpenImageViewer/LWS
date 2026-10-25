#pragma once
#include <LWS/Clipboard.hpp>
#include <LWS/Window.hpp>
#include <functional>
#include <string>
#include <string_view>
namespace LWS
{
    using ClipboardTextCallback = std::function<void(ClipboardResult, std::string)>;
    // UI-thread only; UTF-8 on every platform. The callback may run immediately.
    // Text cannot contain embedded NUL; malformed UTF-8 or NUL-containing text is rejected with UnknownError.
    // A pending read is cancelled when its owner window is destroyed.
    // Callbacks follow the context exception policy and Window dispatch lifetime contract.
    // Wayland reads require client keyboard focus; writes require an input serial.
    ClipboardResult SetClipboardText(Window& owner, std::string_view text);
    void RequestClipboardText(Window& owner, ClipboardTextCallback callback);
}  // namespace LWS
