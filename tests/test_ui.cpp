#ifdef LWS_TEST_UI
    #include <catch2/catch_test_macros.hpp>
    #include <LWSUI/Composites.hpp>
    #include <LWSUI/Menu.hpp>
    #include <LWS/Platform.hpp>
    #ifdef LWS_HAS_WIN32_BACKEND
        #include <LWS/Win32/Platform.hpp>
        #include <LWS/Win32/WindowExtensions.hpp>
        #include <commctrl.h>
    #endif
    #include "source/MenuHost.hpp"
    #include <memory>
    #include <thread>
    #include <string>
    #include <utility>
    #include <vector>

TEST_CASE("UI event dispatch respects connection changes", "[ui][event]")
{
    LWSUI::Event<void()> event;
    std::vector<int> calls;
    LWSUI::Event<void()>::Connection first, second, added;
    first = event.Connect(
        [&]
        {
            calls.push_back(1);
            // Removing the running callback and its successor must not invalidate dispatch.
            first.Disconnect();
            second.Disconnect();
            added = event.Connect([&] { calls.push_back(3); });
        });
    second = event.Connect([&] { calls.push_back(2); });
    event.Raise();
    REQUIRE(calls == std::vector<int>{1});
    event.Raise();
    REQUIRE(calls == std::vector<int>{1, 3});
}

TEST_CASE("UI event connections may outlive their publisher", "[ui][event][lifetime]")
{
    auto event = std::make_unique<LWSUI::Event<void()>>();
    unsigned calls = 0;
    auto destroy = event->Connect(
        [&]
        {
            ++calls;
            event.reset();
        });
    auto later = event->Connect([&] { ++calls; });
    event->Raise();
    REQUIRE_FALSE(event);
    REQUIRE(calls == 1);
    destroy.Disconnect();
    later.Disconnect();
}

TEST_CASE("Text edits preserve UTF-8 and transactional validation", "[ui][text]")
{
    LWSUI::TextBox text("base");
    REQUIRE(text.Dispatch({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::End}));
    std::vector<LWSUI::EditPhase> phases;
    auto edited = text.OnEdit.Connect([&](const std::string&, LWSUI::EditPhase phase) { phases.push_back(phase); });
    REQUIRE(text.Dispatch({.kind = LWSUI::InputKind::Text, .text = "\xc3\xa9\xf0\x9f\x98\x80"}));
    REQUIRE(text.Dispatch({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Backspace}));
    REQUIRE(text.Text() == "base\xc3\xa9");
    text.SetValidation("Rejected draft");
    REQUIRE_FALSE(text.Finish(LWSUI::EditPhase::Commit));
    REQUIRE(text.Finish(LWSUI::EditPhase::Cancel));
    REQUIRE(text.Text() == "base");
    REQUIRE(text.Validation().empty());
    REQUIRE(phases.back() == LWSUI::EditPhase::Cancel);
    REQUIRE(text.Dispatch({.kind = LWSUI::InputKind::Text, .text = "saved\n"}));
    REQUIRE(text.Finish(LWSUI::EditPhase::Commit));
    const auto committed = text.Text();
    REQUIRE(committed.find('\n') == std::string::npos);
    REQUIRE(text.Dispatch({.kind = LWSUI::InputKind::Text, .text = "draft"}));
    text.SetReadOnly(true);
    REQUIRE(text.Text() == committed);
    REQUIRE(text.Dispatch({.kind = LWSUI::InputKind::Text, .text = "ignored"}));
    REQUIRE(text.Text() == committed);
}

TEST_CASE("UI popups preserve rejected drafts and cancel with Escape", "[ui][popup]")
{
    #ifdef LWS_HAS_WIN32_BACKEND
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    #endif
    LWS::PlatformContext context;
    #ifdef LWS_HAS_WIN32_BACKEND
    REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    #else
    REQUIRE(context.Init({.backend = LWS::BackendId::Wayland}) == LWS::Result::Success);
    #endif
    LWS::Window window(context);
    REQUIRE(window.Create({.visible = true}) == LWS::Result::Success);
    LWSUI::UIHost host(window);
    auto root = std::make_unique<LWSUI::Button>("Owner");
    auto* owner = root.get();
    host.SetRoot(std::move(root));
    auto popup = std::make_unique<LWSUI::TextBox>("original");
    auto* editor = popup.get();
    LWSUI::EditPhase closed = LWSUI::EditPhase::Preview;
    REQUIRE(host.OpenPopup(
        *owner, std::move(popup), {0, 0, 200, 100}, [&](LWSUI::EditPhase phase) { closed = phase; }, true));
    REQUIRE(host.Focus(editor));
    REQUIRE(host.Route({.kind = LWSUI::InputKind::Text, .text = "draft"}));
    editor->SetValidation("Rejected draft");
    REQUIRE_FALSE(host.OpenPopup(*owner, std::make_unique<LWSUI::Button>("Replacement"), {0, 0, 200, 100}));
    REQUIRE(host.HasPopup());
    REQUIRE(host.IsFocused(*editor));
    REQUIRE(host.Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Escape}));
    REQUIRE_FALSE(host.HasPopup());
    REQUIRE(closed == LWSUI::EditPhase::Cancel);
    REQUIRE(host.IsFocused(*owner));
}

TEST_CASE("UI context menus defer commands and preserve the text draft", "[ui][menu]")
{
    #ifdef LWS_HAS_WIN32_BACKEND
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    #endif
    LWS::PlatformContext context;
    #ifdef LWS_HAS_WIN32_BACKEND
    REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    #else
    REQUIRE(context.Init({.backend = LWS::BackendId::Wayland}) == LWS::Result::Success);
    #endif
    LWS::Window window(context);
    REQUIRE(window.Create({.visible = true}) == LWS::Result::Success);
    LWSUI::UIHost host(window);
    auto root = std::make_unique<LWSUI::TextBox>("original");
    auto* editor = root.get();
    host.SetRoot(std::move(root));
    REQUIRE(host.Focus(editor));
    REQUIRE(host.Route({.kind = LWSUI::InputKind::Text, .text = "draft"}));
    const auto draft = editor->Text();
    unsigned calls = 0;
    editor->SetContextMenuProvider(
        [&](LWSUI::Control&, const LWSUI::ContextMenuRequest&)
        { return std::vector<LWSUI::ContextMenuItem>{{"Run", "", [&](LWSUI::Control&) { ++calls; }}}; });
    REQUIRE(host.Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::F10, .shift = true}));
    REQUIRE(host.HasContextMenu());
    REQUIRE(host.IsFocused(*editor));
    REQUIRE(host.Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Enter}));
    REQUIRE_FALSE(host.HasContextMenu());
    REQUIRE(calls == 0);
    host.Update();
    REQUIRE(calls == 1);
    REQUIRE(editor->Text() == draft);
    REQUIRE(editor->Finish(LWSUI::EditPhase::Cancel));
    REQUIRE(editor->Text() == "original");
}

namespace
{
    struct MenuCounters
    {
        unsigned fresh = 0, open = 0, recent = 0, docs = 0, quit = 0, undo = 0;
        unsigned Total() const { return fresh + open + recent + docs + quit + undo; }
    };
    std::vector<LWSUI::MenuBarItem> BuildTestMenu(MenuCounters& counts)
    {
        using LWSUI::MenuItem;
        MenuItem open;
        open.label = "&Open";
        open.enabled = false;
        open.action = [&counts] { ++counts.open; };
        MenuItem recent;
        recent.label = "&Recent";
        recent.submenu.push_back(MenuItem{"&Project", "", [&counts] { ++counts.recent; }});
        recent.submenu.push_back(MenuItem::Separator());
        recent.submenu.push_back(MenuItem{"&Documents", "", [&counts] { ++counts.docs; }});
        MenuItem quit;
        quit.label = "E&xit";
        quit.action = [&counts] { ++counts.quit; };
        return {{"&File",
                 {MenuItem{"&New", "Ctrl+N", [&counts] { ++counts.fresh; }}, open, MenuItem::Separator(), recent,
                  quit}},
                {"&Edit", {MenuItem{"&Undo", "Ctrl+Z", [&counts] { ++counts.undo; }}}}};
    }
    struct MenuEnvironment
    {
        MenuEnvironment()
        {
            #ifdef LWS_HAS_WIN32_BACKEND
            REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
            REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
            #else
            REQUIRE(context.Init({.backend = LWS::BackendId::Wayland}) == LWS::Result::Success);
            #endif
            window_ = std::make_unique<LWS::Window>(context);
            REQUIRE(window_->Create({.clientSize = {640, 480}, .visible = true}) == LWS::Result::Success);
            host_ = std::make_unique<LWSUI::UIHost>(*window_);
#ifndef LWS_HAS_WIN32_BACKEND
            // Wayland publishes the drawable viewport asynchronously after the configure handshake.
            for (int attempt = 0; attempt < 500 && !window_->IsConfigured(); ++attempt)
            {
                REQUIRE(context.ProcessMessages() == LWS::LoopResult::Continue);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            REQUIRE(window_->IsConfigured());
#endif
        }
        LWS::Window& window() { return *window_; }
        LWSUI::UIHost& host() { return *host_; }
        LWS::PlatformContext context;

      private:

        std::unique_ptr<LWS::Window> window_;
        std::unique_ptr<LWSUI::UIHost> host_;
    };
    #ifdef LWS_HAS_WIN32_BACKEND
    HWND Hwnd(LWS::Window& window)
    {
        const auto handle = LWS::Win32::GetHwnd(window);
        REQUIRE(handle.has_value());
        return *handle;
    }
    /// Opens File and descends into Recent, leaving two mapped popup levels; returns their HWNDs.
    std::pair<HWND, HWND> OpenCascade(MenuEnvironment& env, LWSUI::internal::MenuSession& session)
    {
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::F10}));
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Down}));
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Down}));
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Right}));
        REQUIRE(session.LevelCount() == 2);
        env.host().Update();
        return {Hwnd(*session.LevelWindow(0)), Hwnd(*session.LevelWindow(1))};
    }
    #endif
}  // namespace

TEST_CASE("Menu window buttons reserve space and defer cancellable close actions", "[ui][menu][window-controls]")
{
    using Access = LWSUI::internal::MenuSessionAccess;
    using Action = LWSUI::internal::MenuWindowAction;
    MenuEnvironment env;
    MenuCounters counts;
    unsigned closed = 0, underlying = 0, errors = 0;
    auto root = std::make_unique<LWSUI::Button>("Underlying content");
    auto click = root->OnClick.Connect([&] { ++underlying; });
    auto error = env.host().OnError.Connect([&](const std::string&) { ++errors; });
    env.host().SetRoot(std::move(root));
    env.host().SetMainMenu(std::make_unique<LWSUI::MenuBar>(BuildTestMenu(counts)));
    auto* bar = env.host().MainMenu();
    bar->SetWindowControls({.minimize = true, .maximize = true, .draggable = true, .requestClose = [&] { ++closed; }});
    env.host().Update();
    const auto pump = [&] { REQUIRE(env.context.ProcessMessages() == LWS::LoopResult::Continue); };
    const auto press = [&]
    {
        const auto b = Access::WindowButtonBounds(*bar, Action::Close);
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Down, .x = b.x + b.width / 2, .y = b.y + b.height / 2}));
        return b;
    };
    const auto release = [&](LWSUI::Rect b)
    { REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Up, .x = b.x + b.width / 2, .y = b.y + b.height / 2})); };
    const auto minimize = Access::WindowButtonBounds(*bar, Action::Minimize);
    const auto maximize = Access::WindowButtonBounds(*bar, Action::Maximize);
    const auto close = Access::WindowButtonBounds(*bar, Action::Close);
    REQUIRE(minimize.x + minimize.width <= maximize.x + .01f);
    REQUIRE(maximize.x + maximize.width <= close.x + .01f);
    REQUIRE(Access::DragBounds(*bar).width >= 48);
    REQUIRE(bar->HitItem(close.x + 1, close.y + 1) == LWSUI::MenuBar::NoItem);
    auto b = press();
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Move, .x = 300, .y = 300}));
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Up, .x = 300, .y = 300}));
    pump(); REQUIRE(closed == 0); REQUIRE(underlying == 0);
    b = press();
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Escape}));
    release(b); pump(); REQUIRE(closed == 0);
    b = press();
    bar->SetItems(BuildTestMenu(counts));
    release(b); pump(); REQUIRE(closed == 0);
    b = press();
    env.host().SetMainMenuDock(LWSUI::MenuDock::Floating);
    release(b); pump(); REQUIRE(closed == 0);
    REQUIRE(Access::WindowButtonBounds(*bar, Action::Close).width == 0);
    REQUIRE(Access::DragBounds(*bar).width == 0);
    env.host().SetMainMenuDock(LWSUI::MenuDock::Bottom); env.host().Update();
    REQUIRE(Access::WindowButtonBounds(*bar, Action::Close).width > 0);
    bar->orientation = LWSUI::Orientation::Vertical;
    env.host().Invalidate(true); env.host().Update();
    REQUIRE(Access::WindowButtonBounds(*bar, Action::Close).width == 0);
    bar->orientation = LWSUI::Orientation::Horizontal;
    env.host().SetMainMenuDock(LWSUI::MenuDock::Top); env.host().Update();
    b = press(); release(b); REQUIRE(closed == 0); pump(); REQUIRE(closed == 1);
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::F10}));
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Left}));
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Enter}));
    REQUIRE(closed == 1); pump(); REQUIRE(closed == 2);
#ifndef LWS_HAS_WIN32_BACKEND
    REQUIRE_FALSE(Access::WindowButtonEnabled(*bar, Action::Minimize));
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Down, .x = minimize.x + 2, .y = minimize.y + 2}));
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Up, .x = minimize.x + 2, .y = minimize.y + 2}));
    pump(); REQUIRE(env.window().IsCreated());
#endif
    // Caption buttons retain their own hit regions even when labels and the drag region do not fit.
    bar->Measure({90, 40}); bar->Arrange({0, 0, 90, 40});
    for (auto action : {Action::Minimize, Action::Maximize, Action::Close})
    {
        const auto r = Access::WindowButtonBounds(*bar, action);
        REQUIRE(r.width > 0);
        REQUIRE(r.x >= 0);
        REQUIRE(r.x + r.width <= 90.01f);
        REQUIRE(bar->HitItem(r.x + r.width / 2, r.y + 2) == LWSUI::MenuBar::NoItem);
    }
    env.host().Invalidate(true); env.host().Update();
    b = press(); release(b);
    env.host().ClearMainMenu(); pump(); REQUIRE(closed == 2);
    REQUIRE(errors == 0);
    REQUIRE(underlying == 0);
}
#ifdef LWS_HAS_WIN32_BACKEND
TEST_CASE("Captionless menu bars use native Windows state and move APIs", "[ui][menu][window-controls]")
{
    using Access = LWSUI::internal::MenuSessionAccess;
    using Action = LWSUI::internal::MenuWindowAction;
    MenuEnvironment env;
    REQUIRE(env.window().SetWindowStyles(LWS::WindowStyle::ResizableBorder) == LWS::Result::Success);
    env.host().SetMainMenu(std::make_unique<LWSUI::MenuBar>());
    auto* bar = env.host().MainMenu();
    bar->SetWindowControls({.minimize = true, .maximize = true, .draggable = true});
    env.host().Update();
    const HWND hwnd = Hwnd(env.window());
    REQUIRE((GetWindowLongW(hwnd, GWL_STYLE) & WS_CAPTION) == 0);
    REQUIRE((GetWindowLongW(hwnd, GWL_STYLE) & WS_THICKFRAME) != 0);
    const auto click = [&](Action action)
    {
        const auto b = Access::WindowButtonBounds(*bar, action);
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Down, .x = b.x + 2, .y = b.y + 2}));
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Up, .x = b.x + 2, .y = b.y + 2}));
        REQUIRE(env.context.ProcessMessages() == LWS::LoopResult::Continue);
        env.host().Update();
    };
    click(Action::Maximize);
    REQUIRE(env.window().GetShowState() == LWS::WindowShowState::Maximized);
    REQUIRE(IsZoomed(hwnd));
    click(Action::Maximize);
    REQUIRE(env.window().GetShowState() == LWS::WindowShowState::Restored);
    REQUIRE_FALSE(IsZoomed(hwnd));
    click(Action::Minimize);
    REQUIRE(IsIconic(hwnd));
    REQUIRE(env.window().RequestShowState(LWS::WindowShowState::Restored) == LWS::Result::Success);
    env.host().Update();
    unsigned moves = 0;
    const SUBCLASSPROC observe = [](HWND h, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) -> LRESULT
    {
        if (message == WM_NCLBUTTONDOWN && w == HTCAPTION) { ++*reinterpret_cast<unsigned*>(data); return 0; }
        return DefSubclassProc(h, message, w, l);
    };
    REQUIRE(SetWindowSubclass(hwnd, observe, 1, reinterpret_cast<DWORD_PTR>(&moves)));
    POINT point{};
    const bool pointerAvailable = GetCursorPos(&point) != FALSE;
    const auto drag = Access::DragBounds(*bar);
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Down, .x = drag.x + 4, .y = drag.y + 4}));
    // LWS cannot issue the native move message on a desktop without pointer access.
    if (pointerAvailable) REQUIRE(moves == 1);
    REQUIRE(RemoveWindowSubclass(hwnd, observe, 1));
    REQUIRE((GetWindowLongW(hwnd, GWL_STYLE) & WS_CAPTION) == 0);
}
#endif
    #ifdef LWS_HAS_WIN32_BACKEND
TEST_CASE("Menu caption double-click is an explicit independent maximize toggle", "[ui][menu][double-click]")
{
    MenuEnvironment env;
    using Action = LWSUI::internal::MenuWindowAction;
    auto menu = std::make_unique<LWSUI::MenuBar>();
    menu->SetWindowControls({.doubleClickMaximize = true});
    env.host().SetMainMenu(std::move(menu));
    env.host().Update();
    auto* bar = env.host().MainMenu();
    REQUIRE_FALSE(bar->WindowControls().maximize);
    REQUIRE_FALSE(bar->WindowControls().draggable);
    SECTION("client double-click")
    {
        const auto scale = *env.window().GetClientAreaMetrics().Scale();
        SendMessageW(Hwnd(env.window()), WM_LBUTTONDBLCLK, MK_LBUTTON,
                     MAKELPARAM(int(100 * scale.x), int(10 * scale.y)));
        REQUIRE(env.window().GetShowState() == LWS::WindowShowState::Restored);
        REQUIRE(env.context.ProcessMessages() == LWS::LoopResult::Continue);
        REQUIRE(env.window().GetShowState() == LWS::WindowShowState::Maximized);
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Down, .x = 100, .y = 10, .clickCount = 2}));
        REQUIRE(env.context.ProcessMessages() == LWS::LoopResult::Continue);
        REQUIRE(env.window().GetShowState() == LWS::WindowShowState::Restored);
    }
    SECTION("custom caption native path after a move request")
    {
        auto controls = bar->WindowControls();
        controls.draggable = true;
        bar->SetWindowControls(controls);
        POINT cursor{};
        if (!GetCursorPos(&cursor))
            SKIP("The interactive desktop is unavailable for native dragging");
        bool requested = false;
        const SUBCLASSPROC observeMove = [](HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR,
                                            DWORD_PTR data) -> LRESULT
        {
            if (message == WM_NCLBUTTONDOWN && w == HTCAPTION)
                *reinterpret_cast<bool*>(data) = true;
            if (message == WM_ENTERSIZEMOVE)
                PostMessageW(hwnd, WM_LBUTTONUP, 0, 0);
            return DefSubclassProc(hwnd, message, w, l);
        };
        REQUIRE(SetWindowSubclass(Hwnd(env.window()), observeMove, 19, reinterpret_cast<DWORD_PTR>(&requested)));
        const auto watchdog = SetTimer(Hwnd(env.window()), 19, 1000, [](HWND hwnd, UINT, UINT_PTR, DWORD)
                                       { SendMessageW(hwnd, WM_CANCELMODE, 0, 0); });
        REQUIRE(watchdog != 0);
        const auto scale = *env.window().GetClientAreaMetrics().Scale();
        POINT point{int(100 * scale.x), int(10 * scale.y)};
        SendMessageW(Hwnd(env.window()), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
        KillTimer(Hwnd(env.window()), watchdog);
        REQUIRE(requested);
        REQUIRE(ClientToScreen(Hwnd(env.window()), &point));
        SendMessageW(Hwnd(env.window()), WM_NCLBUTTONDBLCLK, HTCAPTION, MAKELPARAM(point.x, point.y));
        REQUIRE(env.context.ProcessMessages() == LWS::LoopResult::Continue);
        REQUIRE(env.window().GetShowState() == LWS::WindowShowState::Maximized);
        REQUIRE(RemoveWindowSubclass(Hwnd(env.window()), observeMove, 19));
    }
    SECTION("caption presence does not enable or disable the option")
    {
        REQUIRE(env.window().SetWindowStyles(LWS::WindowStyle::Caption | LWS::WindowStyle::ResizableBorder) ==
                LWS::Result::Success);
        env.host().Update();
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Down, .x = 100, .y = 10, .clickCount = 2}));
        REQUIRE(env.context.ProcessMessages() == LWS::LoopResult::Continue);
        REQUIRE(env.window().GetShowState() == LWS::WindowShowState::Maximized);
    }
    SECTION("disable cancels a queued double-click")
    {
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Down, .x = 100, .y = 10, .clickCount = 2}));
        bar->SetWindowControls({});
        REQUIRE(env.context.ProcessMessages() == LWS::LoopResult::Continue);
        REQUIRE(env.window().GetShowState() == LWS::WindowShowState::Restored);
    }
    SECTION("interactive and disabled content never acts as a caption")
    {
        bar->SetContent(std::make_unique<LWSUI::Button>("Action"));
        env.host().Update();
        for (bool enabled : {true, false})
        {
            bar->Content()->SetEnabled(enabled);
            env.host().Route({.kind = LWSUI::InputKind::Down, .x = 100, .y = 10, .clickCount = 2});
            env.host().Route({.kind = LWSUI::InputKind::Up, .x = 100, .y = 10});
            REQUIRE(env.context.ProcessMessages() == LWS::LoopResult::Continue);
            REQUIRE(env.window().GetShowState() == LWS::WindowShowState::Restored);
        }
    }
    SECTION("default remains disabled")
    {
        bar->SetWindowControls({});
        REQUIRE_FALSE(bar->WindowControls().doubleClickMaximize);
        env.host().Route({.kind = LWSUI::InputKind::Down, .x = 100, .y = 10, .clickCount = 2});
        REQUIRE(env.context.ProcessMessages() == LWS::LoopResult::Continue);
        REQUIRE(env.window().GetShowState() == LWS::WindowShowState::Restored);
    }
}

TEST_CASE("Live resize renders synchronously without deferred work or retirement", "[ui][resize]")
{
    struct Probe : LWSUI::Control
    {
        unsigned paints = 0;
        LWSUI::Rect painted;
        std::function<void()> arranged, rendered;
        void OnArrange() override
        {
            if (arranged)
                arranged();
        }
        void OnRender(LWSUI::Canvas& canvas) override
        {
            if (rendered)
                rendered();
            ++paints;
            painted = Bounds();
            canvas.Fill(painted.x, painted.y, painted.width, painted.height, Style().surface);
        }
    };
    struct Retired : LWSUI::Control
    {
        bool& destroyed;
        explicit Retired(bool& value) : destroyed(value) {}
        ~Retired() { destroyed = true; }
    };
    MenuEnvironment env;
    unsigned errors = 0;
    auto error = env.host().OnError.Connect([&](const auto&) { ++errors; });
    auto root = std::make_unique<Probe>();
    auto* probe = root.get();
    env.host().SetRoot(std::move(root));
    env.host().Update();
    REQUIRE_FALSE(env.host().RedrawOnResize());
    const auto initial = probe->paints;
    REQUIRE(env.window().RequestPlacement({.clientSize = LWS::LogicalSize{660, 490}}) == LWS::Result::Success);
    REQUIRE(probe->paints == initial);
    env.host().Update();
    REQUIRE(probe->painted.width == 660);
    env.host().SetRedrawOnResize(true);
    bool destroyed = false, posted = false, platformTask = false;
    REQUIRE(env.host().OpenPopup(*probe, std::make_unique<Retired>(destroyed), {10, 10, 100, 60}));
    env.host().Post(probe->Handle(), [&](auto&) { posted = true; });
    REQUIRE(env.context.PostTask([&] { platformTask = true; }) == LWS::Result::Success);
    const auto before = probe->paints;
    SendMessageW(Hwnd(env.window()), WM_ENTERSIZEMOVE, 0, 0);
    REQUIRE(env.window().RequestPlacement({.clientSize = LWS::LogicalSize{700, 510}}) == LWS::Result::Success);
    REQUIRE(probe->paints > before);
    REQUIRE(probe->painted.width == 700);
    REQUIRE_FALSE(destroyed);
    REQUIRE_FALSE(posted);
    REQUIRE_FALSE(platformTask);
    REQUIRE_FALSE(env.host().HasPopup());
    SendMessageW(Hwnd(env.window()), WM_EXITSIZEMOVE, 0, 0);
    bool nested = false;
    probe->arranged = [&]
    {
        if (!nested)
        {
            nested = true;
            REQUIRE(env.window().RequestPlacement({.clientSize = LWS::LogicalSize{740, 540}}) == LWS::Result::Success);
        }
    };
    REQUIRE(env.window().RequestPlacement({.clientSize = LWS::LogicalSize{720, 530}}) == LWS::Result::Success);
    REQUIRE(nested);
    REQUIRE(probe->painted.width == 740);
    REQUIRE_FALSE(destroyed);
    REQUIRE_FALSE(posted);
    probe->arranged = {};
    env.host().Update();
    REQUIRE(destroyed);
    REQUIRE(posted);
    REQUIRE_FALSE(platformTask);
    REQUIRE(env.context.ProcessMessages() == LWS::LoopResult::Continue);
    REQUIRE(platformTask);
    SECTION("layout-only invalidation never paints obsolete menu geometry")
    {
        env.host().SetMainMenu(std::make_unique<LWSUI::MenuBar>());
        env.host().Update();
        auto* bar = env.host().MainMenu();
        bool changed = false;
        probe->arranged = [&]
        {
            if (!changed)
            {
                changed = true;
                bar->SetSpan(250);
            }
        };
        probe->rendered = [&] { REQUIRE(bar->Bounds().width == 250); };
        const auto beforeLayout = probe->paints;
        REQUIRE(env.window().RequestPlacement({.clientSize = LWS::LogicalSize{755, 555}}) == LWS::Result::Success);
        REQUIRE(changed);
        REQUIRE(probe->paints == beforeLayout + 1);
    }
    SECTION("child hosts redraw during parent layout")
    {
        LWS::Window child(env.context);
        REQUIRE(child.Create({.parent = &env.window(), .clientSize = {120, 80}, .visible = true}) ==
                LWS::Result::Success);
        LWSUI::UIHost childHost(child);
        childHost.SetRedrawOnResize(true);
        auto childRoot = std::make_unique<Probe>();
        auto* childProbe = childRoot.get();
        childHost.SetRoot(std::move(childRoot));
        childHost.Update();
        const auto beforeChild = childProbe->paints;
        probe->arranged = [&]
        {
            REQUIRE(child.RequestPlacement({.clientSize = LWS::LogicalSize{int(probe->Bounds().width / 2), 100}}) ==
                    LWS::Result::Success);
        };
        REQUIRE(env.window().RequestPlacement({.clientSize = LWS::LogicalSize{780, 580}}) == LWS::Result::Success);
        REQUIRE(childProbe->paints > beforeChild);
        REQUIRE(childProbe->painted.width == 390);
        probe->arranged = {};
    }
    SECTION("minimized zero-sized clients are not presented")
    {
        const auto beforeMinimize = probe->paints;
        REQUIRE(env.window().RequestShowState(LWS::WindowShowState::Minimized) == LWS::Result::Success);
        REQUIRE(probe->paints == beforeMinimize);
        REQUIRE(env.window().RequestShowState(LWS::WindowShowState::Restored) == LWS::Result::Success);
        REQUIRE(probe->paints > beforeMinimize);
    }
    SECTION("disabled runtime option restores deferred redraw")
    {
        env.host().SetRedrawOnResize(false);
        const auto count = probe->paints;
        REQUIRE(env.window().RequestPlacement({.clientSize = LWS::LogicalSize{750, 550}}) == LWS::Result::Success);
        REQUIRE(probe->paints == count);
    }
    SECTION("destroyed during layout never presents a stale frame")
    {
        const auto count = probe->paints;
        probe->arranged = [&] { REQUIRE(env.window().Destroy() == LWS::Result::Success); };
        REQUIRE(env.window().RequestPlacement({.clientSize = LWS::LogicalSize{760, 560}}) == LWS::Result::Success);
        REQUIRE_FALSE(env.window().IsCreated());
        REQUIRE(probe->paints == count);
    }
    REQUIRE(errors == 0);
}
    #endif

TEST_CASE("Close caption hover and press colors are configurable", "[ui][menu][window-controls]")
{
    MenuEnvironment env;
    auto theme = env.host().Style();
    REQUIRE(theme.closeHoverBackground == LLUtils::Color{uint32_t{0xe81123ff}});
    REQUIRE(theme.closePressedBackground == LLUtils::Color{uint32_t{0xc50f1fff}});
    REQUIRE(theme.closeActiveForeground == LLUtils::Color{uint32_t{0xffffffff}});
    theme.closeHoverBackground = LLUtils::Color{uint32_t{0x126543ff}};
    theme.closePressedBackground = LLUtils::Color{uint32_t{0x543210ff}};
    theme.closeActiveForeground = LLUtils::Color{uint32_t{0xfedcbaff}};
    env.host().SetTheme(theme);
    auto menu = std::make_unique<LWSUI::MenuBar>();
    unsigned closed = 0;
    menu->SetWindowControls({.minimize = true, .requestClose = [&] { ++closed; }});
    env.host().SetMainMenu(std::move(menu));
    env.host().Update();
    auto* bar = env.host().MainMenu();
    using Access = LWSUI::internal::MenuSessionAccess;
    using Action = LWSUI::internal::MenuWindowAction;
    const auto close = Access::WindowButtonBounds(*bar, Action::Close);
    const auto check = [&](LLUtils::Color color)
    {
        LWSUI::Canvas canvas;
        canvas.Begin(640, 480);
        bar->Render(canvas);
        const auto bitmap = canvas.End();
        const auto* pixel = bitmap.pixels.data() + int(close.y + 4) * bitmap.rowPitch + int(close.x + 4) * 4;
        REQUIRE(std::to_integer<uint8_t>(pixel[0]) == color.B());
        REQUIRE(std::to_integer<uint8_t>(pixel[1]) == color.G());
        REQUIRE(std::to_integer<uint8_t>(pixel[2]) == color.R());
    };
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Move, .x = close.x + 4, .y = close.y + 4}));
    check(theme.closeHoverBackground);
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Down, .x = close.x + 4, .y = close.y + 4}));
    check(theme.closePressedBackground);
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Up, .x = 1, .y = 100}));
    env.host().Update();
    REQUIRE(closed == 0);
    check(theme.surface);
    bar->SetEnabled(false);
    env.host().Route({.kind = LWSUI::InputKind::Move, .x = close.x + 4, .y = close.y + 4});
    check(theme.surface);
}

TEST_CASE("Menu icon items and owned content share layout and input safely", "[ui][menu][content]")
{
    struct Content final : LWSUI::Container
    {
        LWSUI::Label* title = &Emplace<LWSUI::Label>("Title");
        LWSUI::TextBox* editor = &Emplace<LWSUI::TextBox>("Edit");
        LWSUI::Button* action = &Emplace<LWSUI::Button>("Action");
        LWSUI::Control* HitTest(float x, float y) override
        {
            if (!Bounds().Contains(x, y)) return nullptr;
            for (auto& child : Children()) if (auto* hit = child->HitTest(x, y)) return hit;
            return nullptr;
        }
        LWSUI::Size OnMeasure(LWSUI::Size available) override
        {
            for (auto& child : Children()) child->Measure({80, available.height});
            return {280, available.height};
        }
        void OnArrange() override
        {
            const auto b = Bounds();
            title->Arrange({b.x, b.y, 60, b.height});
            editor->Arrange({b.x + 80, b.y, 80, b.height});
            action->Arrange({b.x + b.width - 80, b.y, 80, b.height});
        }
    };
    using Access = LWSUI::internal::MenuSessionAccess;
    using Action = LWSUI::internal::MenuWindowAction;
    MenuEnvironment env;
    MenuCounters counts;
    env.host().SetRoot(std::make_unique<LWSUI::Button>("Body"));
    auto menu = std::make_unique<LWSUI::MenuBar>(BuildTestMenu(counts));
    auto* bar = menu.get();
    unsigned closed = 0, clicked = 0;
    bar->SetWindowControls({.draggable = true, .requestClose = [&] { ++closed; }});
    std::array<std::byte, 8 * 4 * 4> pixels{};
    const auto icon = std::make_shared<LWS::Bitmap>(LWS::BitmapBuffer{.pixels = pixels, .width = 8, .height = 4, .rowPitch = 32});
    bar->SetIcon(icon);
    auto contents = std::make_unique<Content>();
    auto* controls = contents.get();
    auto click = controls->action->OnClick.Connect([&] { ++clicked; });
    bar->SetContent(std::move(contents), 280);
    env.host().SetMainMenu(std::move(menu));
    env.host().Update();
    REQUIRE(bar->Icon() == icon);
    REQUIRE(bar->ItemRect(0).x > bar->Bounds().x);
    REQUIRE(bar->Content()->Bounds().x >= bar->ItemRect(1).x + bar->ItemRect(1).width);
    const auto close = Access::WindowButtonBounds(*bar, Action::Close);
    REQUIRE(bar->Content()->Bounds().x + bar->Content()->Bounds().width <= close.x + .01f);
    REQUIRE(bar->MinimumWidth() >= 280 + close.width + 40);
    REQUIRE(Access::HitDrag(*bar, 4, 4));
    REQUIRE_FALSE(Access::HitDrag(*bar, bar->ItemRect(0).x + 3, bar->ItemRect(0).y + 3));
    REQUIRE_FALSE(Access::HitDrag(*bar, close.x + 3, close.y + 3));
    for (auto* control : {static_cast<LWSUI::Control*>(controls->title),
                          static_cast<LWSUI::Control*>(controls->editor),
                          static_cast<LWSUI::Control*>(controls->action)})
    {
        const auto bounds = control->Bounds();
        REQUIRE_FALSE(Access::HitDrag(*bar, bounds.x + 3, bounds.y + 3));
    }
    controls->action->SetEnabled(false);
    REQUIRE_FALSE(Access::HitDrag(*bar, controls->action->Bounds().x + 3, controls->action->Bounds().y + 3));
    controls->action->SetEnabled(true);
    const auto captionControls = bar->WindowControls();
    auto noDrag = captionControls; noDrag.draggable = false;
    bar->SetWindowControls(noDrag);
    REQUIRE_FALSE(Access::HitDrag(*bar, 4, 4));
    bar->SetWindowControls(captionControls);
    bar->SetEnabled(false); REQUIRE_FALSE(Access::HitDrag(*bar, 4, 4)); bar->SetEnabled(true);
    const auto press = [&](LWSUI::Rect b)
    { return env.host().Route({.kind = LWSUI::InputKind::Down, .x = b.x + 3, .y = b.y + 3}); };
    const auto release = [&](LWSUI::Rect b)
    { return env.host().Route({.kind = LWSUI::InputKind::Up, .x = b.x + 3, .y = b.y + 3}); };
    REQUIRE(press(controls->action->Bounds())); REQUIRE(release(controls->action->Bounds()));
    REQUIRE(clicked == 1); REQUIRE_FALSE(env.host().HasOpenMenu());
    REQUIRE(env.host().Focus(controls->editor));
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Text, .text = "draft"}));
    const auto draft = controls->editor->Text();
    REQUIRE(press(close));
    REQUIRE(release(controls->action->Bounds())); // Release over content cancels the caption button.
    REQUIRE(env.context.ProcessMessages() == LWS::LoopResult::Continue);
    REQUIRE(closed == 0); REQUIRE(clicked == 1);
#ifdef LWS_HAS_WIN32_BACKEND
    REQUIRE(GetCapture() == nullptr);
    unsigned moves = 0;
    const SUBCLASSPROC observer = [](HWND h, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) -> LRESULT
    { if (message == WM_NCLBUTTONDOWN && w == HTCAPTION) { ++*reinterpret_cast<unsigned*>(data); return 0; }
      return DefSubclassProc(h, message, w, l); };
    REQUIRE(SetWindowSubclass(Hwnd(env.window()), observer, 9, reinterpret_cast<DWORD_PTR>(&moves)));
    POINT point{}; const bool pointer = GetCursorPos(&point) != FALSE;
    REQUIRE(press({1, 1, 20, 20})); REQUIRE(release({1, 1, 20, 20}));
    if (pointer) REQUIRE(moves == 1);
    const auto iconMoves = moves;
    REQUIRE(press(controls->title->Bounds())); REQUIRE(release(controls->title->Bounds()));
    controls->action->SetEnabled(false);
    REQUIRE(press(controls->action->Bounds())); REQUIRE(release(controls->action->Bounds()));
    REQUIRE(moves == iconMoves);
    const auto b = controls->Bounds();
    REQUIRE(press({b.x + 170, b.y, 10, b.height}));
    if (pointer) REQUIRE(moves == iconMoves + 1);
    REQUIRE(release({b.x + 170, b.y, 10, b.height}));
    REQUIRE(RemoveWindowSubclass(Hwnd(env.window()), observer, 9));
    controls->action->SetEnabled(true);
#endif
    REQUIRE(controls->editor->Text() == draft);
    REQUIRE(env.host().Focus(controls->editor));
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Tab}));
    REQUIRE(env.host().IsFocused(*controls->action));
    controls->action->SetContextMenuProvider([](auto&, const auto&)
        { return std::vector<LWSUI::ContextMenuItem>{{"Choose", "", [](auto&) {}}}; });
    const auto actionBounds = controls->action->Bounds();
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Up, .x = actionBounds.x + 2, .y = actionBounds.y + 2, .button = LWS::MouseButton::Right}));
    REQUIRE(env.host().HasContextMenu());
    const auto old = controls->action->Handle();
    bar->SetContent(std::make_unique<LWSUI::Label>("Replacement"), 80);
    REQUIRE_FALSE(old);
    env.host().Update(); REQUIRE_FALSE(env.host().HasContextMenu());
    bar->SetIcon(nullptr); env.host().Update();
    REQUIRE(bar->ItemRect(0).x == bar->Bounds().x);
    bar->SetIcon(icon); bar->orientation = LWSUI::Orientation::Vertical;
    env.host().SetMainMenuDock(LWSUI::MenuDock::Left); env.host().Update();
    REQUIRE(bar->ItemRect(0).y > bar->Bounds().y);
    REQUIRE_FALSE(Access::HitDrag(*bar, 4, 4));
    REQUIRE(bar->Content()->Bounds().width == 0);
}
TEST_CASE("Menu bar docks, floats, and hit-tests items", "[ui][menu]")
{
    MenuEnvironment env;
    MenuCounters counts;
    env.host().SetRoot(std::make_unique<LWSUI::Button>("content"));
    env.host().SetMainMenu(std::make_unique<LWSUI::MenuBar>(BuildTestMenu(counts)));
    auto* bar = env.host().MainMenu();
    REQUIRE(bar != nullptr);
    env.host().Update();
    // Top dock: the bar spans the client width and shrinks the root from above.
    REQUIRE(bar->Bounds().x == 0.f);
    REQUIRE(bar->Bounds().y == 0.f);
    REQUIRE(bar->Bounds().width == 640.f);
    REQUIRE(bar->Bounds().height == bar->Thickness());
    REQUIRE(env.host().Root()->Bounds().y == bar->Thickness());
    REQUIRE(env.host().Root()->Bounds().height == 480.f - bar->Thickness());
    // Item geometry is cumulative and hit testing follows it.
    const auto fileItem = bar->ItemRect(0);
    const auto editItem = bar->ItemRect(1);
    REQUIRE(fileItem.width > 0);
    REQUIRE(editItem.x == fileItem.x + fileItem.width);
    REQUIRE(bar->HitItem(fileItem.x + fileItem.width / 2, fileItem.y + fileItem.height / 2) == 0);
    REQUIRE(bar->HitItem(editItem.x + editItem.width / 2, editItem.y + 1) == 1);
    REQUIRE(bar->HitItem(editItem.x + editItem.width + 4, editItem.y + 1) == LWSUI::MenuBar::NoItem);
    // Bottom dock flips the split.
    env.host().SetMainMenuDock(LWSUI::MenuDock::Bottom);
    env.host().Update();
    REQUIRE(bar->Bounds().y == 480.f - bar->Thickness());
    REQUIRE(env.host().Root()->Bounds().y == 0.f);
    REQUIRE(env.host().Root()->Bounds().height == 480.f - bar->Thickness());
    // Side docks use the vertical meter and inset the root.
    bar->orientation = LWSUI::Orientation::Vertical;
    env.host().SetMainMenuDock(LWSUI::MenuDock::Left);
    env.host().Update();
    REQUIRE(bar->Bounds().width == bar->Thickness());
    REQUIRE(bar->Bounds().height == 480.f);
    REQUIRE(env.host().Root()->Bounds().x == bar->Thickness());
    env.host().SetMainMenuDock(LWSUI::MenuDock::Right);
    env.host().Update();
    REQUIRE(bar->Bounds().x == 640.f - bar->Thickness());
    REQUIRE(env.host().Root()->Bounds().width == 640.f - bar->Thickness());
    // A floating bar overlays the root at the requested origin.
    bar->orientation = LWSUI::Orientation::Horizontal;
    env.host().SetMainMenuDock(LWSUI::MenuDock::Floating);
    env.host().SetMainMenuPosition(40, 30);
    env.host().Update();
    REQUIRE(bar->Bounds().x == 40.f);
    REQUIRE(bar->Bounds().y == 30.f);
    REQUIRE(bar->Bounds().width == bar->NaturalSpan());
    REQUIRE(bar->Bounds().height == bar->Thickness());
    REQUIRE(env.host().Root()->Bounds().x == 0.f);
    REQUIRE(env.host().Root()->Bounds().y == 0.f);
    REQUIRE(env.host().Root()->Bounds().width == 640.f);
    REQUIRE(env.host().Root()->Bounds().height == 480.f);
}

TEST_CASE("Menu pointer input opens, swallows releases, and closes on outside clicks", "[ui][menu]")
{
    MenuEnvironment env;
    MenuCounters counts;
    unsigned clicks = 0;
    auto root = std::make_unique<LWSUI::Button>("content");
    auto click = root->OnClick.Connect([&clicks] { ++clicks; });
    env.host().SetRoot(std::move(root));
    env.host().SetMainMenu(std::make_unique<LWSUI::MenuBar>(BuildTestMenu(counts)));
    auto* bar = env.host().MainMenu();
    env.host().Update();
    auto& session = LWSUI::internal::MenuSessionAccess::Get(env.host());
    const auto item = bar->ItemRect(0);
    const float x = item.x + item.width / 2, y = item.y + item.height / 2;
    // The opening press is consumed and its release is swallowed, so the button under
    // the bar never sees a click.
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Down, .x = x, .y = y}));
    REQUIRE(env.host().HasOpenMenu());
    REQUIRE(session.LevelCount() == 1);
    REQUIRE(session.LevelWindow(0)->GetParent() == &env.window());
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Up, .x = x, .y = y}));
    REQUIRE(env.host().HasOpenMenu());
    env.host().Update();
    REQUIRE(session.Panel(0) != nullptr);
    REQUIRE(session.Panel(0)->Count() == 5);
    // A click outside the bar closes the menu and is swallowed with its release.
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Down, .x = 320, .y = 300}));
    REQUIRE_FALSE(env.host().HasOpenMenu());
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Up, .x = 320, .y = 300}));
    REQUIRE(clicks == 0);
    // With the menu closed the same click reaches the button.
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Down, .x = 320, .y = 300}));
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Up, .x = 320, .y = 300}));
    REQUIRE(clicks == 1);
    // The bar consumes wheel input over itself, and only over itself.
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Wheel, .x = x, .y = y, .wheel = 1}));
    REQUIRE_FALSE(env.host().Route({.kind = LWSUI::InputKind::Wheel, .x = 320, .y = 300, .wheel = 1}));
    // A floating bar intercepts presses over the root instead of leaking them.
    env.host().SetMainMenuDock(LWSUI::MenuDock::Floating);
    env.host().SetMainMenuPosition(40, 30);
    env.host().Update();
    const auto floating = bar->ItemRect(0);
    const float fx = floating.x + floating.width / 2, fy = floating.y + floating.height / 2;
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Down, .x = fx, .y = fy}));
    REQUIRE(env.host().HasOpenMenu());
    REQUIRE(clicks == 1);
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Down, .x = 600, .y = 400}));
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Up, .x = 600, .y = 400}));
    REQUIRE_FALSE(env.host().HasOpenMenu());
    REQUIRE(clicks == 1);
    // A floating drag grip is disabled menu content, not a hole exposing the button beneath it.
    auto items = BuildTestMenu(counts);
    items.insert(items.begin(), {"Grip", {}, false});
    bar->SetItems(std::move(items));
    env.host().Update();
    const auto grip = bar->ItemRect(0);
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Down, .x = grip.x + 4, .y = grip.y + 4}));
#ifdef LWS_HAS_WIN32_BACKEND
    REQUIRE(GetCapture() == nullptr);
#endif
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Up, .x = grip.x + 4, .y = grip.y + 4}));
    REQUIRE(clicks == 1);
}

TEST_CASE("Menu keyboard path toggles, navigates, and activates rows", "[ui][menu]")
{
    MenuEnvironment env;
    MenuCounters counts;
    env.host().SetRoot(std::make_unique<LWSUI::Button>("content"));
    env.host().SetMainMenu(std::make_unique<LWSUI::MenuBar>(BuildTestMenu(counts)));
    auto* bar = env.host().MainMenu();
    env.host().Update();
    auto& session = LWSUI::internal::MenuSessionAccess::Get(env.host());
    // F10 activates the bar; Escape dismisses it. Alt toggles on press and closes on release
    // only while the menu is still "unused".
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::F10}));
    REQUIRE(session.Active());
    REQUIRE_FALSE(session.HasLevels());
    REQUIRE(bar->hot == 0);
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Escape}));
    REQUIRE_FALSE(session.Active());
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Alt}));
    REQUIRE(session.Active());
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyUp, .key = LWS::KeyCode::Alt}));
    REQUIRE_FALSE(session.Active());
    // Reactivating and pressing Down opens the File menu on its first row; a lone Down
    // while the bar is inactive stays unhandled.
    REQUIRE_FALSE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Down}));
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::F10}));
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Down}));
    REQUIRE(session.LevelCount() == 1);
    REQUIRE(session.Panel(0)->Active() == 0);
    env.host().Update();
    // Down skips the disabled entry and the separator. The default hover delay keeps the
    // submenu armed, not open.
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Down}));
    REQUIRE(session.Panel(0)->Active() == 3);
    REQUIRE(session.LevelCount() == 1);
    // Right descends into the submenu; Left pops back out.
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Right}));
    REQUIRE(session.LevelCount() == 2);
    REQUIRE(session.Panel(1)->Active() == 0);
    REQUIRE(session.LevelWindow(1)->GetParent() == session.LevelWindow(0));
    env.host().Update();
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Left}));
    REQUIRE(session.LevelCount() == 1);
    // At depth one Left moves to the previous top menu; Right moves back.
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Left}));
    REQUIRE(bar->hot == 1);
    REQUIRE(session.LevelCount() == 1);
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Right}));
    REQUIRE(bar->hot == 0);
    REQUIRE(session.LevelCount() == 1);
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Escape}));
    REQUIRE_FALSE(env.host().HasOpenMenu());
    // A mnemonic select is delivered as text, opens the Edit menu, and Enter runs the row.
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Alt}));
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Text, .text = "e"}));
    REQUIRE(bar->hot == 1);
    REQUIRE(session.LevelCount() == 1);
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Enter}));
    REQUIRE_FALSE(env.host().HasOpenMenu());
    REQUIRE(counts.undo == 0);
    env.host().Update();
    REQUIRE(counts.undo == 1);
    REQUIRE(counts.Total() == 1);
}

TEST_CASE("Menu dropdown rows activate, cascade, and reuse windows", "[ui][menu]")
{
    MenuEnvironment env;
    MenuCounters counts;
    env.host().SetRoot(std::make_unique<LWSUI::Button>("content"));
    env.host().SetMainMenu(std::make_unique<LWSUI::MenuBar>(BuildTestMenu(counts)));
    auto* bar = env.host().MainMenu();
    env.host().Update();
    auto& session = LWSUI::internal::MenuSessionAccess::Get(env.host());
    const auto clickBar = [&]
    {
        const auto item = bar->ItemRect(0);
        const float x = item.x + item.width / 2, y = item.y + item.height / 2;
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Down, .x = x, .y = y}));
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Up, .x = x, .y = y}));
        REQUIRE(session.LevelCount() == 1);
        env.host().Update();
    };
    const auto pressRow = [&](size_t level, size_t row)
    {
        const auto rect = session.Panel(level)->RowRect(row);
        return session.PanelInput(level, {.kind = LWSUI::InputKind::Down, .x = rect.x + 4,
                                          .y = rect.y + rect.height / 2}) &&
               session.PanelInput(level, {.kind = LWSUI::InputKind::Up, .x = rect.x + 4,
                                          .y = rect.y + rect.height / 2});
    };
    clickBar();
    REQUIRE(session.Panel(0)->Count() == 5);
    // Press-and-release on the cascade row opens the submenu as a popup owned by the first level.
    REQUIRE(pressRow(0, 3));
    REQUIRE(session.LevelCount() == 2);
    REQUIRE(session.LevelWindow(1)->GetParent() == session.LevelWindow(0));
    REQUIRE(session.Panel(1)->Count() == 3);
    env.host().Update();
    // Activating a leaf row closes the chain and posts the command to the next update.
    REQUIRE(pressRow(1, 0));
    REQUIRE_FALSE(env.host().HasOpenMenu());
    REQUIRE(counts.recent == 0);
    env.host().Update();
    REQUIRE(counts.recent == 1);
    // Switching while a cascade is open replaces the root panel and retains its Win32 window.
    clickBar();
    REQUIRE(pressRow(0, 3));
    REQUIRE(session.LevelCount() == 2);
    LWS::Window* const first = session.LevelWindow(0);
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Text, .text = "e"}));
    REQUIRE(bar->hot == 1);
    REQUIRE(session.LevelCount() == 1);
    REQUIRE(session.LevelWindow(0)->IsCreated());
    #ifdef LWS_HAS_WIN32_BACKEND
    REQUIRE(session.LevelWindow(0) == first);
    #endif
    // Dismissal releases everything back to the session.
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Escape}));
    REQUIRE(session.LevelCount() == 0);
    REQUIRE(session.LevelWindow(0) == nullptr);
    env.host().Update();
}

TEST_CASE("Menu hover opens cascades on a delay", "[ui][menu]")
{
    MenuEnvironment env;
    MenuCounters counts;
    env.host().SetRoot(std::make_unique<LWSUI::Button>("content"));
    env.host().SetMainMenu(std::make_unique<LWSUI::MenuBar>(BuildTestMenu(counts)));
    env.host().Update();
    auto& session = LWSUI::internal::MenuSessionAccess::Get(env.host());
    const auto openFile = [&]
    {
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::F10}));
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Down}));
        REQUIRE(session.LevelCount() == 1);
        env.host().Update();
    };
    const auto hoverRow = [&](size_t row)
    {
        const auto rect = session.Panel(0)->RowRect(row);
        return session.PanelInput(0, {.kind = LWSUI::InputKind::Move, .x = rect.x + 4,
                                      .y = rect.y + rect.height / 2});
    };
    openFile();
    // The default delay arms a timer; the harness never runs the loop, so nothing opens.
    REQUIRE(hoverRow(3));
    REQUIRE(session.Panel(0)->Active() == 3);
    REQUIRE(session.LevelCount() == 1);
    // A zero delay resolves the cascade synchronously.
    LWSUI::Theme immediate;
    immediate.menuHoverDelayMs = 0;
    env.host().SetTheme(immediate);
    env.host().Update();
    openFile();
    REQUIRE(hoverRow(3));
    REQUIRE(session.LevelCount() == 2);
    REQUIRE(session.Panel(1)->Active() == 0);
    // Moving onto a row without a submenu collapses the cascade again.
    REQUIRE(hoverRow(4));
    REQUIRE(session.LevelCount() == 1);
    REQUIRE(session.Panel(0)->Active() == 4);
}

TEST_CASE("Menu structural changes close the open menu and drop pending commands", "[ui][menu]")
{
    MenuEnvironment env;
    MenuCounters counts, replacement;
    env.host().SetRoot(std::make_unique<LWSUI::Button>("content"));
    env.host().SetMainMenu(std::make_unique<LWSUI::MenuBar>(BuildTestMenu(counts)));
    auto* bar = env.host().MainMenu();
    env.host().Update();
    auto& session = LWSUI::internal::MenuSessionAccess::Get(env.host());
    const auto open = [&]
    {
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::F10}));
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Down}));
        REQUIRE(session.HasLevels());
    };
    // Replacing the definitions while the menu is open discards the old rows entirely.
    open();
    bar->SetItems(BuildTestMenu(replacement));
    REQUIRE_FALSE(env.host().HasOpenMenu());
    env.host().Update();
    REQUIRE(counts.Total() == 0);
    REQUIRE(replacement.Total() == 0);
    open();
    env.host().SetTheme(LWSUI::Theme{});
    REQUIRE_FALSE(env.host().HasOpenMenu());
    open();
    env.host().SetMainMenuDock(LWSUI::MenuDock::Floating);
    REQUIRE_FALSE(env.host().HasOpenMenu());
    REQUIRE(env.host().MainMenuDock() == LWSUI::MenuDock::Floating);
    open();
    env.host().SetRoot(std::make_unique<LWSUI::Button>("other"));
    REQUIRE_FALSE(env.host().HasOpenMenu());
    open();
    env.host().ClearMainMenu();
    REQUIRE_FALSE(env.host().HasMainMenu());
    REQUIRE_FALSE(env.host().HasOpenMenu());
}

#ifdef LWS_HAS_WIN32_BACKEND
TEST_CASE("Floating menu dropdowns track native mouse movement", "[ui][menu]")
{
    MenuEnvironment env;
    MenuCounters counts;
    REQUIRE(env.window().SetWindowStyles(LWS::WindowStyle::Caption | LWS::WindowStyle::CloseButton |
                                        LWS::WindowStyle::ResizableBorder) == LWS::Result::Success);
    env.host().SetRoot(std::make_unique<LWSUI::Button>("content"));
    env.host().SetMainMenu(std::make_unique<LWSUI::MenuBar>(BuildTestMenu(counts)), LWSUI::MenuDock::Floating);
    env.host().SetMainMenuPosition(40, 30);
    env.host().Update();
    auto& session = LWSUI::internal::MenuSessionAccess::Get(env.host());
    const auto item = env.host().MainMenu()->ItemRect(0);
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Move, .x = item.x + 4, .y = item.y + 4}));
    REQUIRE(env.host().MainMenu()->hot == 0);
    REQUIRE_FALSE(env.host().HasOpenMenu());
    SendMessageW(Hwnd(env.window()), WM_MOUSELEAVE, 0, 0);
    REQUIRE(env.host().MainMenu()->hot == -1);
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Down, .x = item.x + 4, .y = item.y + 4}));
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Up, .x = item.x + 4, .y = item.y + 4}));
    env.host().Update();
    REQUIRE(session.LevelCount() == 1);
    auto* panel = session.Panel(0);
    const HWND popup = Hwnd(*session.LevelWindow(0));
    const auto scale = session.LevelWindow(0)->GetClientAreaMetrics().Scale();
    REQUIRE(scale.has_value());
    for (size_t row : {4U, 0U, 3U})
    {
        const auto bounds = panel->RowRect(row);
        const int x = int((bounds.x + 4) * scale->x);
        const int y = int((bounds.y + bounds.height / 2) * scale->y);
        POINT screen{x, y};
        REQUIRE(ClientToScreen(popup, &screen));
        REQUIRE(GetWindow(popup, GW_OWNER) == Hwnd(env.window()));
        REQUIRE(SendMessageW(popup, WM_NCHITTEST, 0, MAKELPARAM(screen.x, screen.y)) == HTCLIENT);
        SendMessageW(popup, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
        REQUIRE(panel->Active() == row);
        env.host().Update();
        // Inspect the panel's rendered output, not desktop pixels that another app can cover.
        LWSUI::Canvas canvas;
        canvas.Begin(int(panel->Bounds().width), int(panel->Bounds().height));
        panel->Render(canvas);
        const auto bitmap = canvas.End();
        const auto* pixel = bitmap.pixels.data() + int(bounds.y + bounds.height / 2) * bitmap.rowPitch + int(bounds.x + 4) * 4;
        const auto color = panel->Style().selection;
        CHECK(std::to_integer<uint8_t>(pixel[0]) == color.B());
        CHECK(std::to_integer<uint8_t>(pixel[1]) == color.G());
        CHECK(std::to_integer<uint8_t>(pixel[2]) == color.R());
    }
    const auto leaf = panel->RowRect(4);
    const LPARAM point = MAKELPARAM(int((leaf.x + 4) * scale->x), int((leaf.y + leaf.height / 2) * scale->y));
    SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, point);
    SendMessageW(popup, WM_LBUTTONUP, 0, point);
    REQUIRE_FALSE(env.host().HasOpenMenu());
    env.host().Update();
    REQUIRE(counts.quit == 1);
}

TEST_CASE("Detached menu dropdowns size independently of their owner", "[ui][menu]")
{
    MenuEnvironment env;
    MenuCounters counts;
    LWS::LogicalSize size{220, 36};
    bool vertical = false;
    SECTION("horizontal") {}
    SECTION("vertical") { size = {60, 220}; vertical = true; }
    REQUIRE(env.window().RequestPlacement({.clientSize = size}) == LWS::Result::Success);
    env.host().SetMainMenu(std::make_unique<LWSUI::MenuBar>(BuildTestMenu(counts)), LWSUI::MenuDock::Floating);
    env.host().MainMenu()->orientation = vertical ? LWSUI::Orientation::Vertical : LWSUI::Orientation::Horizontal;
    env.host().Update();
    auto& session = LWSUI::internal::MenuSessionAccess::Get(env.host());
    const auto item = env.host().MainMenu()->ItemRect(0);
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Down, .x = item.x + 4, .y = item.y + 4}));
    env.host().Update();
    REQUIRE(session.LevelCount() == 1);
    const auto* panel = session.Panel(0);
    const auto last = panel->RowRect(panel->Count() - 1);
    REQUIRE(panel->Bounds().height >= last.y + last.height);
    REQUIRE(panel->Bounds().width >= panel->NaturalWidth(env.host()));
}
TEST_CASE("Menu popups close when the owner window moves", "[ui][menu]")
{
    MenuEnvironment env;
    MenuCounters counts;
    env.host().SetRoot(std::make_unique<LWSUI::Button>("content"));
    env.host().SetMainMenu(std::make_unique<LWSUI::MenuBar>(BuildTestMenu(counts)));
    env.host().Update();
    auto& session = LWSUI::internal::MenuSessionAccess::Get(env.host());
    const auto [dropdown, submenu] = OpenCascade(env, session);
    REQUIRE(IsWindowVisible(dropdown));
    REQUIRE(IsWindowVisible(submenu));
    // Dragging the caption moves the owner without any client input or focus change, which would
    // otherwise leave both levels behind at the screen positions they were anchored to.
    RECT owner{};
    REQUIRE(GetWindowRect(Hwnd(env.window()), &owner));
    SetWindowPos(Hwnd(env.window()), nullptr, owner.left + 120, owner.top + 80, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    REQUIRE_FALSE(env.host().HasOpenMenu());
    REQUIRE(session.LevelCount() == 0);
    REQUIRE_FALSE(IsWindowVisible(dropdown));
    REQUIRE_FALSE(IsWindowVisible(submenu));
    REQUIRE(counts.Total() == 0);
    // Closed levels unmap immediately and are destroyed after their input dispatch has returned.
    env.host().Update();
    REQUIRE_FALSE(IsWindow(submenu));
}

TEST_CASE("Menu popups unmap at once when the owner loses focus", "[ui][menu]")
{
    MenuEnvironment env;
    MenuCounters counts;
    env.host().SetRoot(std::make_unique<LWSUI::Button>("content"));
    env.host().SetMainMenu(std::make_unique<LWSUI::MenuBar>(BuildTestMenu(counts)));
    env.host().Update();
    auto& session = LWSUI::internal::MenuSessionAccess::Get(env.host());
    const auto [dropdown, submenu] = OpenCascade(env, session);
    // Dismissal has to reach the screen immediately, not at the next update that reaps the levels.
    SendMessage(Hwnd(env.window()), WM_KILLFOCUS, 0, 0);
    REQUIRE_FALSE(env.host().HasOpenMenu());
    REQUIRE(session.LevelCount() == 0);
    REQUIRE_FALSE(IsWindowVisible(dropdown));
    REQUIRE_FALSE(IsWindowVisible(submenu));
    // Foregrounding the window again must not bring the dismissed surfaces back.
    SendMessage(Hwnd(env.window()), WM_SETFOCUS, 0, 0);
    REQUIRE_FALSE(env.host().HasOpenMenu());
    REQUIRE_FALSE(IsWindowVisible(dropdown));
    REQUIRE_FALSE(IsWindowVisible(submenu));
    env.host().Update();
    REQUIRE_FALSE(IsWindowVisible(dropdown));
    REQUIRE_FALSE(IsWindowVisible(submenu));
}
#endif

TEST_CASE("Menus preserve captured root gestures and overlay priority", "[ui][menu][routing]")
{
    MenuEnvironment env;
    MenuCounters counts;
    env.host().SetMainMenu(std::make_unique<LWSUI::MenuBar>(BuildTestMenu(counts)));
    SECTION("root capture ends even when released over the menu")
    {
        auto slider = std::make_unique<LWSUI::Slider>();
        unsigned commits = 0, previews = 0;
        auto edit = slider->OnEdit.Connect([&](double, LWSUI::EditPhase phase)
        { commits += phase == LWSUI::EditPhase::Commit; previews += phase == LWSUI::EditPhase::Preview; });
        env.host().SetRoot(std::move(slider));
        env.host().Update();
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Down, .x = 100, .y = 100}));
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Move, .x = 200, .y = 5}));
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Up, .x = 200, .y = 5}));
        REQUIRE(commits == 1);
        const auto completed = previews;
        env.host().Route({.kind = LWSUI::InputKind::Move, .x = 400, .y = 100});
        REQUIRE(previews == completed);
#ifdef LWS_HAS_WIN32_BACKEND
        REQUIRE(GetCapture() == nullptr);
#endif
    }
    SECTION("modal popup blocks pointer and keyboard menu activation")
    {
        auto root = std::make_unique<LWSUI::Button>("owner");
        auto* owner = root.get();
        env.host().SetRoot(std::move(root));
        env.host().Update();
        auto popup = std::make_unique<LWSUI::TextBox>("draft");
        auto* editor = popup.get();
        REQUIRE(env.host().OpenPopup(*owner, std::move(popup), {100, 100, 200, 100}, {}, true));
        REQUIRE(env.host().Focus(editor));
        env.host().Update();
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::F10}));
        REQUIRE_FALSE(env.host().HasOpenMenu());
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Down, .x = 10, .y = 5}));
        REQUIRE(env.host().HasPopup());
        REQUIRE_FALSE(env.host().HasOpenMenu());
    }
    SECTION("context menu owns outside clicks and F10")
    {
        auto root = std::make_unique<LWSUI::Button>("owner");
        auto* owner = root.get();
        env.host().SetRoot(std::move(root));
        env.host().Update();
        REQUIRE(env.host().ShowContextMenu(*owner, {.target = owner->Handle(), .x = 100, .y = 100},
                                           {{"Action", "", [](auto&) {}}}));
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::F10}));
        REQUIRE(env.host().HasContextMenu());
        REQUIRE_FALSE(env.host().HasOpenMenu());
        REQUIRE(env.host().Route({.kind = LWSUI::InputKind::Down, .x = 10, .y = 5}));
        REQUIRE_FALSE(env.host().HasContextMenu());
        REQUIRE_FALSE(env.host().HasOpenMenu());
    }
}

TEST_CASE("Floating menu bounds preserve either orientation", "[ui][menu][layout]")
{
    MenuEnvironment env;
    MenuCounters counts;
    auto bar = std::make_unique<LWSUI::MenuBar>(BuildTestMenu(counts));
    SECTION("horizontal") {}
    SECTION("vertical") { bar->orientation = LWSUI::Orientation::Vertical; }
    SECTION("vertical with icon")
    {
        bar->orientation = LWSUI::Orientation::Vertical;
        std::array<std::byte, 4 * 4 * 4> pixels{};
        bar->SetIcon(std::make_shared<LWS::Bitmap>(
            LWS::BitmapBuffer{.pixels = pixels, .width = 4, .height = 4, .rowPitch = 16}));
    }
    env.host().SetMainMenu(std::move(bar), LWSUI::MenuDock::Floating);
    env.host().SetMainMenuPosition(1000, 1000);
    env.host().Update();
    auto* menu = env.host().MainMenu();
    const auto bounds = menu->Bounds();
    REQUIRE(bounds.width == menu->DesiredSize().width);
    REQUIRE(bounds.height == menu->DesiredSize().height);
    REQUIRE(bounds.x + bounds.width <= 640);
    REQUIRE(bounds.y + bounds.height <= 480);
    const auto last = menu->ItemRect(1);
    REQUIRE(menu->HitItem(last.x + 1, last.y + 1) == 1);
    if (menu->Icon())
    {
        // A vertical icon reserves height; it must not clip the leading part of menu rows.
        menu->hot = 0;
        LWSUI::Canvas canvas;
        canvas.Begin(640, 480);
        menu->Render(canvas);
        const auto bitmap = canvas.End();
        const auto item = menu->ItemRect(0);
        const auto* pixel = bitmap.pixels.data() + int(item.y + item.height / 2) * bitmap.rowPitch +
                            int(item.x + 4) * 4;
        REQUIRE(std::to_integer<uint8_t>(pixel[0]) == menu->Style().selection.B());
        REQUIRE(std::to_integer<uint8_t>(pixel[1]) == menu->Style().selection.G());
        REQUIRE(std::to_integer<uint8_t>(pixel[2]) == menu->Style().selection.R());
    }
}

TEST_CASE("Dropdown windows host scrolling and keyboard layout locally", "[ui][menu][scroll]")
{
    MenuEnvironment env;
    auto theme = env.host().Style(); theme.menuMaxRows = 4;
    env.host().SetTheme(theme);
    std::vector<LWSUI::MenuItem> items;
    for (int i = 0; i < 24; ++i) items.push_back({"Item " + std::to_string(i), "", [] {}});
    env.host().SetMainMenu(std::make_unique<LWSUI::MenuBar>(
        std::vector<LWSUI::MenuBarItem>{{"File", std::move(items)}}));
    env.host().Update();
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::F10}));
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Down}));
    auto& session = LWSUI::internal::MenuSessionAccess::Get(env.host());
    REQUIRE(session.LevelCount() == 1);
    for (int i = 0; i < 500 && !session.LevelWindow(0)->IsConfigured(); ++i)
    {
        REQUIRE(env.context.ProcessMessages() == LWS::LoopResult::Continue);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    auto* panel = session.Panel(0);
    REQUIRE(&panel->Host()->Window() == session.LevelWindow(0));
    env.host().Update();
    REQUIRE(session.PanelInput(0, {.kind = LWSUI::InputKind::Wheel, .x = 4, .y = 4, .wheel = -1}));
    REQUIRE(panel->Offset() > 0);
    env.host().Update();
    REQUIRE(panel->RowRect(0).y < 0);
    const auto bounds = panel->Bounds();
    const float x = bounds.width - panel->Style().scrollbarMargin - panel->Style().scrollbarWidth / 2;
    REQUIRE(dynamic_cast<LWSUI::ScrollBar*>(panel->HitTest(x, bounds.height / 2)));
    REQUIRE(session.PanelInput(0, {.kind = LWSUI::InputKind::Down, .x = x, .y = bounds.height / 2}));
#ifdef LWS_HAS_WIN32_BACKEND
    REQUIRE(GetCapture() == Hwnd(*session.LevelWindow(0)));
#endif
    REQUIRE(session.PanelInput(0, {.kind = LWSUI::InputKind::Move, .x = x, .y = bounds.height}));
    REQUIRE(session.PanelInput(0, {.kind = LWSUI::InputKind::Up, .x = x, .y = bounds.height}));
    REQUIRE(panel->Offset() > 100);
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Home}));
    env.host().Update();
    REQUIRE(panel->Active() == 0);
    REQUIRE(panel->RowRect(0).y == 0);
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::End}));
    env.host().Update();
    REQUIRE(panel->Active() == 23);
    const auto last = panel->RowRect(23);
    REQUIRE(last.y + last.height <= bounds.height + .01f);
}

TEST_CASE("Leaving a submenu row cancels delayed opening", "[ui][menu][hover]")
{
    MenuEnvironment env;
    MenuCounters counts;
    auto theme = env.host().Style(); theme.menuHoverDelayMs = 15;
    env.host().SetTheme(theme);
    env.host().SetMainMenu(std::make_unique<LWSUI::MenuBar>(BuildTestMenu(counts)));
    env.host().Update();
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::F10}));
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Down}));
    auto& session = LWSUI::internal::MenuSessionAccess::Get(env.host());
    const auto move = [&](size_t row)
    {
        const auto b = session.Panel(0)->RowRect(row);
        REQUIRE(session.PanelInput(0, {.kind = LWSUI::InputKind::Move, .x = b.x + 4, .y = b.y + b.height / 2}));
    };
    move(3);
    SECTION("separator") { move(2); }
    SECTION("disabled row") { move(1); }
    REQUIRE_FALSE(session.Panel(0)->Active());
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    REQUIRE(env.context.ProcessMessages() == LWS::LoopResult::Continue);
    REQUIRE(session.LevelCount() == 1);
    REQUIRE_FALSE(session.Panel(0)->Active());
}

TEST_CASE("Scrolling preserves child edits and clips hit testing", "[ui][scroll]")
{
    auto content = std::make_unique<LWSUI::StackPanel>();
    auto& editor = content->Emplace<LWSUI::TextBox>("original");
    for (int index = 0; index < 20; ++index)
        content->Emplace<LWSUI::Button>("Row");
    LWSUI::ScrollView scroll;
    scroll.SetContent(std::move(content));
    scroll.Measure({300, 100});
    scroll.Arrange({0, 0, 300, 100});
    REQUIRE(editor.Dispatch({.kind = LWSUI::InputKind::Text, .text = "draft"}));
    const auto draft = editor.Text();
    REQUIRE(scroll.Dispatch({.kind = LWSUI::InputKind::Wheel, .wheel = -1}));
    REQUIRE(scroll.Offset() > 0);
    scroll.Arrange({0, 0, 300, 100});
    REQUIRE(scroll.HitTest(2, -1) == nullptr);
    REQUIRE(editor.Text() == draft);
    scroll.SetOffset(0);
    scroll.Arrange({0, 0, 300, 100});
    REQUIRE(scroll.HitTest(2, 2) == &editor);
    REQUIRE(editor.Finish(LWSUI::EditPhase::Cancel));
    REQUIRE(editor.Text() == "original");
}

TEST_CASE("SplitPanel owns panes and clamps resizing in both orientations", "[ui][split]")
{
    using namespace LWSUI;
    SplitPanel split(std::make_unique<Label>("First"), std::make_unique<TextBox>("Second"));
    REQUIRE(split.First().Parent() == &split);
    REQUIRE(split.Second().Parent() == &split);
    REQUIRE(split.Children().size() == 3);
    split.SetMinimumSizes(60, 40);
    split.SetRatio(.75f);
    split.SetDividerSize(8);
    split.Measure({200, 208});
    split.Arrange({0, 0, 200, 208});
    REQUIRE(split.First().Bounds().height == 150);
    REQUIRE(split.Second().Bounds().height == 50);
    split.SetDefaultTrailingSize(80, .25f);
    split.Measure({200, 208});
    split.Arrange({0, 0, 200, 208});
    REQUIRE(split.Second().Bounds().height == 50);
    REQUIRE(split.Divider().Dispatch({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Up}));
    split.Arrange({0, 0, 200, 208});
    REQUIRE(split.Second().Bounds().height == 60);
    split.Reset();
    split.Arrange({0, 0, 200, 208});
    REQUIRE(split.Second().Bounds().height == 50);
    split.SetOrientation(Orientation::Horizontal);
    split.Measure({58, 200});
    split.Arrange({0, 0, 58, 200});
    REQUIRE(split.First().Bounds().width == 30);
    REQUIRE(split.Second().Bounds().width == 20);
    REQUIRE(split.Divider().cursor == LWS::CursorShape::SizeEW);
    REQUIRE_THROWS_AS(split.SetRatio(std::numeric_limits<float>::quiet_NaN()), std::invalid_argument);
}
TEST_CASE("Slider value presentation preserves normalized and exact typed editing", "[ui][slider]")
{
    using namespace LWSUI;
    Slider slider;
    slider.SetValue(.25);
    REQUIRE(slider.ValueDisplay() == SliderValueMode::Hidden);
    REQUIRE(slider.ValueField() == nullptr);
    slider.SetValueDisplay(SliderValueMode::ReadOnly);
    REQUIRE(dynamic_cast<Label*>(slider.ValueField()) != nullptr);
    REQUIRE(slider.Value() == .25);
    slider.SetValueDisplay(SliderValueMode::Editable);
    REQUIRE(dynamic_cast<NumericEdit<double>*>(slider.ValueField()) != nullptr);
    slider.Measure({300, 40});
    slider.Arrange({0, 0, 300, 40});
    REQUIRE(slider.TrackBounds().width < slider.Bounds().width);
    REQUIRE(slider.TrackBounds().x + slider.TrackBounds().width <= slider.ValueField()->Bounds().x);
    slider.SetValueDisplay(SliderValueMode::Hidden);
    REQUIRE(slider.ValueField() == nullptr);
    REQUIRE(slider.TrackBounds().width == 300);
    auto& value = slider.ConfigureValue<int64_t>({9007199254740987LL, 9007199254740991LL, 1}, 9007199254740990LL);
    REQUIRE(slider.Dispatch({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Right}));
    REQUIRE(value.Value() == 9007199254740991LL);
    REQUIRE(slider.Dispatch({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Left}));
    REQUIRE(value.Value() == 9007199254740990LL);
    REQUIRE_THROWS_AS(slider.ConfigureValue<int64_t>({0, 0, 1}, 0), std::invalid_argument);
    REQUIRE(value.Value() == 9007199254740990LL);
    REQUIRE_THROWS_AS(slider.SetValue(std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
    auto* number = dynamic_cast<NumericEdit<int64_t>*>(slider.ValueField());
    REQUIRE(number);
    value.SetValue(9007199254740987LL);
    REQUIRE(number->Value() == value.Value());
    auto& decimal = slider.ConfigureValue<double>({.5, 3., .1}, 1.03);
    slider.Measure({300, 40});
    slider.Arrange({0, 0, 300, 40});
    REQUIRE(slider.Dispatch({.kind = InputKind::Down, .x = 0, .y = 2}));
    REQUIRE(decimal.Value() == .5);
    REQUIRE(slider.Finish(EditPhase::Cancel));
    REQUIRE(decimal.Value() == 1.03);
    REQUIRE(slider.Dispatch({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Right}));
    REQUIRE(decimal.Value() == 1.13);
    std::vector<EditPhase> phases;
    auto edits = decimal.OnEdit.Connect([&](double, EditPhase phase) { phases.push_back(phase); });
    REQUIRE(slider.Dispatch({.kind = InputKind::Down, .x = 0, .y = 2}));
    REQUIRE(slider.Dispatch({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Right}));
    REQUIRE(decimal.Value() == .6);
    REQUIRE(phases == std::vector<EditPhase>{EditPhase::Preview, EditPhase::Commit, EditPhase::Commit});
    REQUIRE(slider.Finish(EditPhase::Cancel));
    REQUIRE(slider.Dispatch({.kind = InputKind::Up}));
    REQUIRE(decimal.Value() == .6);
    REQUIRE(phases.size() == 3);
    ScrollBar scroll;
    scroll.SetValue(.4);
    scroll.Measure({20, 200});
    scroll.Arrange({0, 0, 20, 200});
    REQUIRE(scroll.Value() == .4);
    REQUIRE(scroll.ValueField() == nullptr);
}
TEST_CASE("Intrinsic stacks preserve glyph sizing and flows wrap actions", "[ui][layout]")
{
    using namespace LWSUI;
    StackPanel stack;
    stack.orientation = Orientation::Horizontal;
    stack.sizeToContent = true;
    auto& glyph = stack.Emplace<GlyphButton>(Glyph::ChevronDown);
    stack.Emplace<Button>("A much longer action");
    const auto size = stack.Measure({400, 40});
    stack.Arrange({0, 0, size.width, size.height});
    REQUIRE(glyph.Bounds().width == glyph.Bounds().height);
    FlowPanel flow;
    flow.minimumItemWidth = 100;
    flow.firstLeading = flow.alignEnd = true;
    for (auto text : {"Reset", "Cancel", "Apply", "Revert", "Save"})
        flow.AddItem(std::make_unique<Button>(text));
    const auto wrapped = flow.Measure({400, 300});
    flow.Arrange({0, 0, 400, wrapped.height});
    REQUIRE(flow.Children().back()->Bounds().y > flow.Children().front()->Bounds().y);
    for (auto& child : flow.Children())
        REQUIRE(child->Bounds().x + child->Bounds().width <= 400);
}
#endif
