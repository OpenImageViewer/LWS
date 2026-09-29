#ifdef LWS_TEST_UI
    #include <catch2/catch_test_macros.hpp>
    #include <LWSUI/Composites.hpp>
    #include <LWSUI/Menu.hpp>
    #include <LWS/Platform.hpp>
    #ifdef LWS_HAS_WIN32_BACKEND
        #include <LWS/Win32/Platform.hpp>
        #include <LWS/Win32/WindowExtensions.hpp>
    #endif
    #include "source/MenuHost.hpp"
    #include <memory>
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
    // A mnemonic switch while a cascade is open closes the levels and reuses the pooled window.
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
    REQUIRE(session.PooledWindows() == 0);
    #endif
    // Dismissal releases everything back to the session.
    REQUIRE(env.host().Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Escape}));
    REQUIRE(session.LevelCount() == 0);
    REQUIRE(session.LevelWindow(0) == nullptr);
    env.host().Update();
    #ifdef LWS_HAS_WIN32_BACKEND
    REQUIRE(session.PooledWindows() == 0);
    #endif
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
    // The cascade is the level that cannot be pooled, so it is unmapped at once and destroyed here.
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

#endif
