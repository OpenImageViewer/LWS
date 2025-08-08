#pragma once
#include <cstdint>

namespace LWS
{
    /// Platform-agnostic virtual key codes.
    enum class KeyCode : uint32_t
    {
        Unknown = 0,
        // Letters
        A, B, C, D, E, F, G, H, I, J, K, L, M,
        N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        // Digits
        Digit0, Digit1, Digit2, Digit3, Digit4,
        Digit5, Digit6, Digit7, Digit8, Digit9,
        // Function keys
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        // Navigation
        Left, Right, Up, Down,
        Home, End, PageUp, PageDown,
        Insert, Delete,
        // Control
        Enter, Escape, Tab, Backspace, Space,
        Shift, Control, Alt, Win,
        LShift, RShift, LControl, RControl, LAlt, RAlt,
        CapsLock, NumLock, ScrollLock,
        // Numpad
        Numpad0, Numpad1, Numpad2, Numpad3, Numpad4,
        Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
        NumpadAdd, NumpadSubtract, NumpadMultiply, NumpadDivide, NumpadDecimal, NumpadEnter,
        // Punctuation / misc
        Comma, Period, Slash, Semicolon, Quote, LeftBracket, RightBracket, Backslash,
        Minus, Equals, Tilde,
        PrintScreen, Pause,
        // Mouse-adjacent
        LButton, RButton, MButton,
    };
}
