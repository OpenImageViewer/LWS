#include <catch2/catch_test_macros.hpp>
#ifdef LWS_HAS_WIN32_BACKEND
    #include <LWS/Window.hpp>
    #include <LWS/Win32/WindowExtensions.hpp>
    #include "Win32TestSupport.hpp"
    #include <Windows.h>
    #include <commctrl.h>

TEST_CASE("Owned modal top-levels stay independent from child windows", "[win32][owned-modal]")
{
    LwsTest::Context context;
    LWS::Window owner(context.value), dialog(context.value);
    REQUIRE(owner.Create() == LWS::Result::Success);
    REQUIRE(dialog.Create({.owner = &owner, .modal = true}) == LWS::Result::Success);
    const auto parent = *LWS::Win32::GetHwnd(owner);
    const auto child = *LWS::Win32::GetHwnd(dialog);
    CHECK(dialog.GetOwner() == &owner);
    CHECK(dialog.GetParent() == nullptr);
    CHECK_FALSE(dialog.IsChild());
    CHECK(GetWindow(child, GW_OWNER) == parent);
    CHECK((GetWindowLongPtrW(child, GWL_STYLE) & WS_CHILD) == 0);
    CHECK((GetWindowLongPtrW(child, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0);
    CHECK((GetWindowLongPtrW(child, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0);
    CHECK(IsWindowEnabled(parent));
    REQUIRE(dialog.SetVisible(true) == LWS::Result::Success);
    CHECK_FALSE(IsWindowEnabled(parent));
    REQUIRE(dialog.SetVisible(true) == LWS::Result::Success);
    REQUIRE(dialog.SetVisible(false) == LWS::Result::Success);
    CHECK(IsWindowEnabled(parent));
    REQUIRE(dialog.SetVisible(true) == LWS::Result::Success);
    ShowWindow(child, SW_HIDE);
    REQUIRE(dialog.SetVisible(false) == LWS::Result::Success);
    CHECK(IsWindowEnabled(parent));
    REQUIRE(dialog.RequestShowState(LWS::WindowShowState::Restored) == LWS::Result::Success);
    CHECK_FALSE(IsWindowEnabled(parent));
    REQUIRE(dialog.Destroy() == LWS::Result::Success);
    CHECK(IsWindowEnabled(parent));
    CHECK_FALSE(dialog.IsCreated());
    CHECK(dialog.GetOwner() == nullptr);
}

TEST_CASE("Modal owner disabling respects other dialogs and existing disabled state", "[win32][owned-modal]")
{
    LwsTest::Context context;
    LWS::Window owner(context.value), first(context.value), second(context.value);
    REQUIRE(owner.Create() == LWS::Result::Success);
    const auto parent = *LWS::Win32::GetHwnd(owner);
    REQUIRE(first.Create({.owner = &owner, .modal = true, .visible = true}) == LWS::Result::Success);
    REQUIRE(second.Create({.owner = &owner, .modal = true, .visible = true}) == LWS::Result::Success);
    CHECK_FALSE(IsWindowEnabled(parent));
    REQUIRE(first.SetVisible(false) == LWS::Result::Success);
    CHECK_FALSE(IsWindowEnabled(parent));
    REQUIRE(second.Destroy() == LWS::Result::Success);
    CHECK(IsWindowEnabled(parent));
    EnableWindow(parent, FALSE);
    REQUIRE(first.SetVisible(true) == LWS::Result::Success);
    REQUIRE(first.SetVisible(false) == LWS::Result::Success);
    CHECK_FALSE(IsWindowEnabled(parent));
    EnableWindow(parent, TRUE);
}

TEST_CASE("Owned window families validate owners and tear down together", "[win32][owned-modal]")
{
    LwsTest::Context context;
    LWS::Window owner(context.value), child(context.value), dialog(context.value), nested(context.value),
        invalid(context.value);
    REQUIRE(owner.Create() == LWS::Result::Success);
    REQUIRE(child.Create({.parent = &owner}) == LWS::Result::Success);
    CHECK(invalid.Create({.modal = true}) == LWS::Result::InvalidArgument);
    CHECK(invalid.Create({.parent = &owner, .owner = &owner}) == LWS::Result::InvalidArgument);
    CHECK(invalid.Create({.owner = &invalid}) == LWS::Result::InvalidArgument);
    CHECK(invalid.Create({.owner = &child}) == LWS::Result::InvalidArgument);
    REQUIRE(dialog.Create({.owner = &owner, .modal = true, .visible = true}) == LWS::Result::Success);
    REQUIRE(nested.Create({.owner = &dialog, .modal = true, .visible = true}) == LWS::Result::Success);
    REQUIRE(owner.Destroy() == LWS::Result::Success);
    CHECK_FALSE(child.IsCreated());
    CHECK_FALSE(dialog.IsCreated());
    CHECK_FALSE(nested.IsCreated());
    CHECK(dialog.GetOwner() == nullptr);
    CHECK(nested.GetOwner() == nullptr);
}
TEST_CASE("Modal owners and their child controls reject input until the dialog hides", "[win32][owned-modal]")
{
    LwsTest::Context context;
    LWS::Window owner(context.value), control(context.value), dialog(context.value);
    REQUIRE(owner.Create() == LWS::Result::Success);
    REQUIRE(control.Create({.parent = &owner}) == LWS::Result::Success);
    REQUIRE(dialog.Create({.owner = &owner, .modal = true}) == LWS::Result::Success);
    unsigned ownerKeys = 0, controlKeys = 0, dialogKeys = 0;
    auto listener = [](unsigned& count)
    {
        return [&count](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventKeyDown>(event))
                ++count;
            return LWS::EventResponse::Unhandled;
        };
    };
    auto ownerListener = owner.Listen(listener(ownerKeys));
    auto controlListener = control.Listen(listener(controlKeys));
    auto dialogListener = dialog.Listen(listener(dialogKeys));
    REQUIRE(ownerListener);
    REQUIRE(controlListener);
    REQUIRE(dialogListener);
    const auto sendKey = [](LWS::Window& window) { SendMessageW(*LWS::Win32::GetHwnd(window), WM_KEYDOWN, 'A', 0); };
    sendKey(owner);
    CHECK(ownerKeys == 1);
    REQUIRE(dialog.SetVisible(true) == LWS::Result::Success);
    sendKey(owner);
    sendKey(control);
    sendKey(dialog);
    CHECK(ownerKeys == 1);
    CHECK(controlKeys == 0);
    CHECK(dialogKeys == 1);
    SendMessageW(*LWS::Win32::GetHwnd(owner), WM_CLOSE, 0, 0);
    CHECK(owner.IsCreated());
    REQUIRE(dialog.SetVisible(false) == LWS::Result::Success);
    sendKey(owner);
    sendKey(control);
    CHECK(ownerKeys == 2);
    CHECK(controlKeys == 1);
}
TEST_CASE("Modal transitions tolerate synchronous native owner callbacks", "[win32][owned-modal]")
{
    struct CallbackState
    {
        LWS::Window* dialog{};
        bool onEnable{};
        bool called{};
        LWS::Result result{LWS::Result::Success};
    } state;
    LwsTest::Context context;
    LWS::Window owner(context.value), dialog(context.value);
    REQUIRE(owner.Create() == LWS::Result::Success);
    REQUIRE(dialog.Create({.owner = &owner, .modal = true}) == LWS::Result::Success);
    state.dialog = &dialog;
    const auto hwnd = *LWS::Win32::GetHwnd(owner);
    const auto callback = +[](HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR,
                              DWORD_PTR data) -> LRESULT
    {
        auto& state = *reinterpret_cast<CallbackState*>(data);
        if (message == WM_ENABLE && (wparam != 0) == state.onEnable && !state.called)
        {
            state.called = true;
            state.result = state.dialog->Destroy();
        }
        return DefSubclassProc(window, message, wparam, lparam);
    };
    SECTION("Destroying the dialog while disabling its owner aborts showing")
    {
        REQUIRE(SetWindowSubclass(hwnd, callback, 1, reinterpret_cast<DWORD_PTR>(&state)));
        CHECK(dialog.SetVisible(true) == LWS::Result::InvalidState);
        CHECK(state.called);
        CHECK(state.result == LWS::Result::Success);
        CHECK_FALSE(dialog.IsCreated());
        CHECK(IsWindowEnabled(hwnd));
        RemoveWindowSubclass(hwnd, callback, 1);
    }
    SECTION("Hiding an already hidden native dialog still checks callback destruction")
    {
        REQUIRE(dialog.SetVisible(true) == LWS::Result::Success);
        ShowWindow(*LWS::Win32::GetHwnd(dialog), SW_HIDE);
        state.onEnable = true;
        REQUIRE(SetWindowSubclass(hwnd, callback, 1, reinterpret_cast<DWORD_PTR>(&state)));
        CHECK(dialog.SetVisible(false) == LWS::Result::InvalidState);
        CHECK(state.called);
        CHECK(state.result == LWS::Result::Success);
        CHECK_FALSE(dialog.IsCreated());
        CHECK(IsWindowEnabled(hwnd));
        RemoveWindowSubclass(hwnd, callback, 1);
    }
    SECTION("Reentrant destruction while enabling the owner is rejected")
    {
        REQUIRE(dialog.SetVisible(true) == LWS::Result::Success);
        state.onEnable = true;
        REQUIRE(SetWindowSubclass(hwnd, callback, 1, reinterpret_cast<DWORD_PTR>(&state)));
        CHECK(dialog.Destroy() == LWS::Result::Success);
        CHECK(state.called);
        CHECK(state.result == LWS::Result::InvalidState);
        CHECK(IsWindowEnabled(hwnd));
        RemoveWindowSubclass(hwnd, callback, 1);
    }
}
#endif
