#include "SnappingMenu.hpp"
#include <LWSUI/UIHost.hpp>
#include "Showcase.hpp"
#include <LWSUI/Composites.hpp>
#include <LWS/Platform.hpp>
#ifdef LWS_HAS_WIN32_BACKEND
    #include <LWS/Win32/Platform.hpp>
#endif
#include <LWS/Timer.hpp>
#include <iostream>
#include <iomanip>
#include <fstream>
#ifdef _WIN32
    #include <windows.h>
#endif
int LWSUI::demo::RunShowcase(int argc, char** argv)
{
    bool smoke = false, snapshot = false, narrow = false;
    for (int i = 1; i < argc; ++i)
    {
        std::string argument = argv[i];
        smoke |= argument == "--smoke";
        snapshot |= argument == "--snapshot" || argument == "--snapshot-narrow";
        narrow |= argument == "--snapshot-narrow";
    }
#ifdef LWS_HAS_WIN32_BACKEND
    if (LWS::Win32::BootstrapProcess() != LWS::Result::Success)
        throw std::runtime_error("Cannot initialize the Windows process");
#endif
    LWS::PlatformContext platform;
    const LWS::PlatformConfig platformConfig{
#ifdef LWS_HAS_WIN32_BACKEND
        .backend = LWS::BackendId::Win32,
#else
        .backend = LWS::BackendId::Wayland,
#endif
    };
    if (platform.Init(platformConfig) != LWS::Result::Success)
        throw std::runtime_error("Cannot initialize the showcase platform");
    LWS::Window window(platform);
    LWS::WindowConfig config;
    config.title = LWSUI::NativeText("LWSUI / Menu Gallery - fixed size");
    config.clientSize = {560, 620};
    config.clientSizeLimits = {{560, 620}, {560, 620}};
    config.visible = true;
    config.backgroundColor = LWSUI::MakeTheme(DefaultThemePreset).background;
    config.styles = LWS::WindowStyle::Caption | LWS::WindowStyle::CloseButton;
    if (window.Create(config) != LWS::Result::Success)
        throw std::runtime_error("Cannot create the menu gallery");
    struct MenuDragState
    {
        bool armed = false, dragging = false;
        float pointerX = 0, pointerY = 0, barX = 0, barY = 0;
    } menuDrag;
    LWSUI::UIHost* uiHost = nullptr;
    auto dragListener = window.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (!uiHost)
                return LWS::EventResponse::Unhandled;
            if (const auto* button = std::get_if<LWS::EventMouseButton>(&event))
            {
                if (button->button != LWS::MouseButton::Left)
                    return LWS::EventResponse::Unhandled;
                if (!button->pressed)
                {
                    const bool wasDragging = menuDrag.dragging;
                    menuDrag.armed = false;
                    menuDrag.dragging = false;
                    return wasDragging ? LWS::EventResponse::Handled : LWS::EventResponse::Unhandled;
                }
                menuDrag.armed = false;
                menuDrag.dragging = false;
                auto* bar = uiHost->MainMenu();
                // Only the grip starts a drag. Arming on menu labels can close their
                // dropdown as soon as the pointer moves between press and release.
                if (uiHost->MainMenuDock() == LWSUI::MenuDock::Floating && bar &&
                    bar->HitItem(float(button->position.x), float(button->position.y)) == 0)
                {
                    menuDrag.armed = true;
                    menuDrag.pointerX = float(button->position.x);
                    menuDrag.pointerY = float(button->position.y);
                    menuDrag.barX = bar->Bounds().x;
                    menuDrag.barY = bar->Bounds().y;
                }
                return LWS::EventResponse::Unhandled;
            }
            if (const auto* move = std::get_if<LWS::EventMouseMove>(&event);
                move && menuDrag.armed && uiHost->MainMenuDock() == LWSUI::MenuDock::Floating)
            {
                const float deltaX = float(move->position.x) - menuDrag.pointerX;
                const float deltaY = float(move->position.y) - menuDrag.pointerY;
                if (!menuDrag.dragging && deltaX * deltaX + deltaY * deltaY < 16.f)
                    return LWS::EventResponse::Unhandled;
                if (!menuDrag.dragging)
                {
                    menuDrag.dragging = true;
                    uiHost->CloseMainMenu();
                }
                uiHost->SetMainMenuPosition(menuDrag.barX + deltaX, menuDrag.barY + deltaY);
                return LWS::EventResponse::Handled;
            }
            if (std::holds_alternative<LWS::EventMouseLeave>(event) ||
                std::holds_alternative<LWS::EventMouseCaptureLost>(event) ||
                std::holds_alternative<LWS::EventFocusLost>(event))
            {
                menuDrag.armed = false;
                menuDrag.dragging = false;
            }
            return LWS::EventResponse::Unhandled;
        });
    if (!dragListener)
        return 3;
    LWSUI::UIHost host(window, LWSUI::MakeTheme(DefaultThemePreset));
    uiHost = &host;
    LWSUI::demo::Showcase showcase(platform, host);
    if (snapshot)
    {
        const LWS::LogicalSize workspaceSize = narrow ? LWS::LogicalSize{800, 580} : LWS::LogicalSize{1100, 760};
        const LWS::LogicalSize gallerySize = narrow ? LWS::LogicalSize{420, 600} : LWS::LogicalSize{600, 720};
        if (showcase.MainWindow().RequestPlacement({.clientSize = workspaceSize}) != LWS::Result::Success)
            return 3;
        for (auto sample : showcase.Samples())
            if (sample.type == "TextBox" &&
                sample.control->Host()->Window().RequestPlacement({.clientSize = gallerySize}) != LWS::Result::Success)
                return 3;
    }
    struct MenuState
    {
        LWSUI::ThemePreset preset = DefaultThemePreset;
        LWSUI::MenuDock dock = LWSUI::MenuDock::Left;
        bool vertical = true, detached = false;
    };
    struct DetachedMenu
    {
        std::unique_ptr<LWS::Window> window;
        std::unique_ptr<LWSUI::UIHost> host;
        LWS::EventConnection dragListener;
        LWS::EventConnection closeListener;
        LWS::EventConnection snappingListener;
    };
    constexpr float kDetachedPadding = 8.f;
    constexpr float kDetachedExtra = 64.f;
    MenuState menuState;
    std::unique_ptr<DetachedMenu> detached;
    auto lifetime = std::make_shared<int>(0);
    bool transitionPending = false;
    const auto command = [&showcase](std::string name)
    { return [&showcase, name = std::move(name)] { showcase.Command(name); }; };
    std::function<void()> refresh;
    std::function<void(bool)> setDetached;
    // Demo-only coupling: a side dock reads as a vertical bar and a vertical bar belongs on a side,
    // so choosing either one places the other. LWSUI keeps dock and orientation independent.
    const auto applyPlacement = [&host, &detached, &menuState, &refresh](LWSUI::MenuDock dock, bool vertical)
    {
        menuState.dock = dock;
        menuState.vertical = vertical;
        auto* menuHost = menuState.detached && detached ? detached->host.get() : &host;
        menuHost->SetMainMenuDock(menuState.detached ? LWSUI::MenuDock::Floating : dock);
        if (auto* bar = menuHost->MainMenu())
            bar->orientation = vertical ? LWSUI::Orientation::Vertical : LWSUI::Orientation::Horizontal;
        menuHost->Invalidate(true);
        refresh();
    };
    const auto onSide = [&menuState]
    { return menuState.dock == LWSUI::MenuDock::Left || menuState.dock == LWSUI::MenuDock::Right; };
    const auto dockBar = [&applyPlacement, &menuState](LWSUI::MenuDock to)
    {
        const bool side = to == LWSUI::MenuDock::Left || to == LWSUI::MenuDock::Right;
        const bool across = to == LWSUI::MenuDock::Top || to == LWSUI::MenuDock::Bottom;
        // Floating has no edge to agree with, so it keeps the orientation the bar already has.
        applyPlacement(to, side ? true : across ? false : menuState.vertical);
    };
    const auto orientBar = [&applyPlacement, &menuState, onSide](bool vertical)
    {
        // Stay on the current edge when it already suits the new orientation.
        const auto dock = vertical
                              ? (onSide() ? menuState.dock : LWSUI::MenuDock::Left)
                              : (menuState.dock == LWSUI::MenuDock::Top || menuState.dock == LWSUI::MenuDock::Bottom
                                     ? menuState.dock
                                     : LWSUI::MenuDock::Top);
        applyPlacement(dock, vertical);
    };
    const auto buildItems = [&](const MenuState& state, LWS::Window* detachedWindow = nullptr)
    {
        const auto theme = [&state, &showcase](LWSUI::ThemePreset preset, const char* label)
        {
            return LWSUI::MenuItem{.label = label,
                                   .action = [&showcase, preset] { showcase.SetTheme(preset); },
                                   .checked = state.preset == preset};
        };
        const auto dockItem = [&dockBar, &state](LWSUI::MenuDock dock, const char* label)
        {
            return LWSUI::MenuItem{.label = label,
                                   .action = [&dockBar, dock] { dockBar(dock); },
                                   .checked = state.dock == dock};
        };
        std::vector<LWSUI::MenuBarItem> items{
            {"&File",
             {LWSUI::MenuItem{.label = "&New scene", .action = command("file/new")},
              LWSUI::MenuItem{.label = "&Load sample", .action = command("file/open")},
              LWSUI::MenuItem{.label = "&Reset values", .action = command("file/save")}, LWSUI::MenuItem::Separator(),
              LWSUI::MenuItem{
                  .label = "&Recent",
                  .submenu =
                      {LWSUI::MenuItem{.label = "&Project",
                                       .submenu = {LWSUI::MenuItem{.label = "main.cpp",
                                                                   .action = command("file/recent/main.cpp")},
                                                   LWSUI::MenuItem{.label = "settings.json",
                                                                   .action = command("file/recent/settings.json")},
                                                   LWSUI::MenuItem{.label = "CMakeLists.txt",
                                                                   .action = command("file/recent/CMakeLists.txt")}}},
                       LWSUI::MenuItem{.label = "&Documents",
                                       .submenu = {LWSUI::MenuItem{.label = "notes.txt",
                                                                   .action = command("file/recent/notes.txt")},
                                                   LWSUI::MenuItem{.label = "report.md",
                                                                   .action = command("file/recent/report.md")}}}}},
              LWSUI::MenuItem::Separator(),
              LWSUI::MenuItem{.label = "E&xit", .action = [&platform] { platform.RequestQuit(); }}}},
            {"&Edit",
             {LWSUI::MenuItem{.label = "&Undo", .shortcut = "Ctrl+Z", .enabled = false},
              LWSUI::MenuItem{.label = "&Redo", .shortcut = "Ctrl+Y", .enabled = false}, LWSUI::MenuItem::Separator(),
              LWSUI::MenuItem{.label = "Cu&t example", .action = command("edit/cut")},
              LWSUI::MenuItem{.label = "&Copy example", .action = command("edit/copy")},
              LWSUI::MenuItem{.label = "&Paste example", .action = command("edit/paste")}}},
            {"&View",
             {LWSUI::MenuItem{.label = "&Theme",
                              .submenu = {theme(LWSUI::ThemePreset::Dark, "&Dark"),
                                          theme(LWSUI::ThemePreset::Light, "&Light"),
                                          theme(LWSUI::ThemePreset::Warm, "&Warm")}},
              LWSUI::MenuItem{
                  .label = "Menu b&ar",
                  .submenu = {LWSUI::MenuItem{.label = "&Detached",
                                              .action = [&setDetached, &state] { setDetached(!state.detached); },
                                              .checked = state.detached},
                              LWSUI::MenuItem{.label = "&Dock",
                                              .enabled = !state.detached,
                                              .submenu = {dockItem(LWSUI::MenuDock::Top, "&Top"),
                                                          dockItem(LWSUI::MenuDock::Bottom, "&Bottom"),
                                                          dockItem(LWSUI::MenuDock::Left, "&Left"),
                                                          dockItem(LWSUI::MenuDock::Right, "&Right"),
                                                          dockItem(LWSUI::MenuDock::Floating, "&Floating")}},
                              LWSUI::MenuItem{.label = "&Vertical",
                                              .action = [&orientBar, &state] { orientBar(!state.vertical); },
                                              .checked = state.vertical}}}}},
            {"&Help", {LWSUI::MenuItem{.label = "&About", .action = command("help/about")}}}};
        auto snapping = SnappingMenu(window, [&refresh] { refresh(); });
        if (detachedWindow)
        {
            snapping.label = "&Menu Gallery";
            auto detachedSnapping = SnappingMenu(*detachedWindow, [&refresh] { refresh(); });
            detachedSnapping.label = "&Detached menu window";
            items[2].items.push_back({.label = "&Snapping",
                                      .enabled = snapping.enabled,
                                      .submenu = {std::move(snapping), std::move(detachedSnapping)}});
        }
        else
            items[2].items.push_back(std::move(snapping));
        if (state.detached || state.dock == LWSUI::MenuDock::Floating)
            items.insert(items.begin(), {"⠿", {}, false});
        return items;
    };
    refresh = [&host, &detached, &menuState, &buildItems, &showcase]
    {
        if (auto* bar = host.MainMenu())
            bar->SetItems(buildItems(menuState));
        if (detached)
            if (auto* bar = detached->host->MainMenu())
            {
                bar->SetItems(buildItems(menuState, detached->window.get()));
                const float span = bar->NaturalSpan();
                const float thickness = bar->Thickness();
                const auto resized = detached->window->RequestPlacement(LWS::WindowPlacementRequest{
                    .clientSize = menuState.vertical ? LWS::LogicalSize{int32_t(thickness + kDetachedPadding),
                                                                        int32_t(span + kDetachedPadding)}
                                                     : LWS::LogicalSize{int32_t(span + kDetachedPadding),
                                                                        int32_t(thickness + kDetachedPadding)}});
                if (resized != LWS::Result::Success)
                    showcase.Log("Error: Cannot resize detached menu window");
            }
    };
    const auto weakSnappingLifetime = std::weak_ptr(lifetime);
    const auto observeSnapping = [&](LWS::Window& observed)
    {
        return observed.Listen(
            [&, weakSnappingLifetime](const LWS::AnyEvent& event)
            {
                if (std::holds_alternative<LWS::EventSnappingStateChanged>(event) && !weakSnappingLifetime.expired())
                {
                    const auto result = platform.PostTask(
                        [&, weakSnappingLifetime]
                        {
                            if (!weakSnappingLifetime.expired())
                                refresh();
                        });
                    if (result != LWS::Result::Success)
                        showcase.Log("Error: Cannot refresh snapping state");
                }
                return LWS::EventResponse::Unhandled;
            });
    };
    auto snappingListener = observeSnapping(window);
    if (!snappingListener)
        return 3;
    host.SetMainMenu(std::make_unique<LWSUI::MenuBar>(buildItems(menuState)), menuState.dock);
    host.MainMenu()->orientation = LWSUI::Orientation::Vertical;
    showcase.OnThemeChanged = [&](LWSUI::ThemePreset preset)
    {
        menuState.preset = preset;
        if (detached)
            detached->host->SetTheme(LWSUI::MakeTheme(preset));
        refresh();
    };
    setDetached = [&](bool detach)
    {
        if (transitionPending || menuState.detached == detach)
            return;
        transitionPending = true;
        const auto weakLifetime = std::weak_ptr(lifetime);
        if (platform.PostTask(
                [&, weakLifetime, detach]
                {
                    if (weakLifetime.expired())
                        return;
                    transitionPending = false;
                    if (menuState.detached == detach)
                        return;
                    if (detach)
                    {
                        auto* currentBar = host.MainMenu();
                        if (!currentBar)
                            return;
                        const float span = currentBar->NaturalSpan();
                        const float thickness = currentBar->Thickness();
                        auto candidate = std::make_unique<DetachedMenu>();
                        candidate->window = std::make_unique<LWS::Window>(platform);
                        LWS::WindowConfig detachedConfig;
                        detachedConfig.title = LWSUI::NativeText("");
                        detachedConfig.clientSize = menuState.vertical
                                                        ? LWS::LogicalSize{int32_t(thickness + kDetachedPadding),
                                                                           int32_t(span + kDetachedExtra)}
                                                        : LWS::LogicalSize{int32_t(span + kDetachedExtra),
                                                                           int32_t(thickness + kDetachedPadding)};
                        detachedConfig.styles = LWS::WindowStyle::NoStyle;
                        if (platform.GetBackendId() == LWS::BackendId::Win32)
                            detachedConfig.snappingRole = LWS::WindowSnappingRole::Follower;
                        detachedConfig.backgroundColor = LWSUI::MakeTheme(menuState.preset).background;
                        if (candidate->window->Create(detachedConfig) != LWS::Result::Success)
                        {
                            showcase.Log("Error: Cannot create detached menu window");
                            return;
                        }
                        auto snapping = observeSnapping(*candidate->window);
                        if (!snapping)
                        {
                            showcase.Log("Error: Cannot observe detached menu snapping");
                            return;
                        }
                        candidate->snappingListener = std::move(*snapping);
                        auto* detachedWindow = candidate->window.get();
                        auto* candidateMenu = candidate.get();
                        auto dragListener = detachedWindow->Listen(
                            [candidateMenu, detachedWindow](const LWS::AnyEvent& event)
                            {
                                const auto* button = std::get_if<LWS::EventMouseButton>(&event);
                                if (!button || !button->pressed || button->button != LWS::MouseButton::Left ||
                                    !candidateMenu->host)
                                    return LWS::EventResponse::Unhandled;
                                auto* bar = candidateMenu->host->MainMenu();
                                if (!bar || bar->HitItem(float(button->position.x), float(button->position.y)) != 0)
                                    return LWS::EventResponse::Unhandled;
                                return detachedWindow->BeginWindowDrag(LWS::WindowDragOperation::Move) ==
                                               LWS::Result::Success
                                           ? LWS::EventResponse::Handled
                                           : LWS::EventResponse::Unhandled;
                            });
                        if (!dragListener)
                        {
                            showcase.Log("Error: Cannot listen for detached menu dragging");
                            return;
                        }
                        candidate->dragListener = std::move(*dragListener);
                        candidate->host = std::make_unique<LWSUI::UIHost>(*candidate->window,
                                                                          LWSUI::MakeTheme(menuState.preset));
                        menuState.detached = true;
                        auto bar = std::make_unique<LWSUI::MenuBar>(buildItems(menuState, detachedWindow));
                        bar->orientation = menuState.vertical ? LWSUI::Orientation::Vertical
                                                              : LWSUI::Orientation::Horizontal;
                        candidate->host->SetMainMenu(std::move(bar), LWSUI::MenuDock::Floating);
                        const auto* detachedBar = candidate->host->MainMenu();
                        const float detachedSpan = detachedBar->NaturalSpan();
                        const float detachedThickness = detachedBar->Thickness();
                        const auto resized = detachedWindow->RequestPlacement(LWS::WindowPlacementRequest{
                            .clientSize = menuState.vertical
                                              ? LWS::LogicalSize{int32_t(detachedThickness + kDetachedPadding),
                                                                 int32_t(detachedSpan + kDetachedPadding)}
                                              : LWS::LogicalSize{int32_t(detachedSpan + kDetachedPadding),
                                                                 int32_t(detachedThickness + kDetachedPadding)}});
                        if (resized != LWS::Result::Success)
                        {
                            menuState.detached = false;
                            showcase.Log("Error: Cannot size detached menu window");
                            return;
                        }
                        auto closeListener = detachedWindow->Listen(
                            [&, weakLifetime, detachedWindow](const LWS::AnyEvent& event)
                            {
                                if (weakLifetime.expired())
                                    return LWS::EventResponse::Unhandled;
                                if (std::holds_alternative<LWS::EventWindowDestroyed>(event) && detached &&
                                    detached->window.get() == detachedWindow)
                                    setDetached(false);
                                return LWS::EventResponse::Unhandled;
                            });
                        if (!closeListener)
                        {
                            menuState.detached = false;
                            showcase.Log("Error: Cannot listen to detached menu window");
                            return;
                        }
                        candidate->closeListener = std::move(*closeListener);
                        if (detachedWindow->SetVisible(true) != LWS::Result::Success)
                        {
                            menuState.detached = false;
                            showcase.Log("Error: Cannot show detached menu window");
                            return;
                        }
                        detached = std::move(candidate);
                        host.ClearMainMenu();
                        refresh();
                        return;
                    }
                    menuState.detached = false;
                    auto bar = std::make_unique<LWSUI::MenuBar>(buildItems(menuState));
                    bar->orientation = menuState.vertical ? LWSUI::Orientation::Vertical
                                                          : LWSUI::Orientation::Horizontal;
                    host.SetMainMenu(std::move(bar), menuState.dock);
                    if (detached)
                    {
                        detached->host->ClearMainMenu();
                        detached->closeListener.Disconnect();
                        detached.reset();
                    }
                    refresh();
                }) != LWS::Result::Success)
        {
            transitionPending = false;
            showcase.Log("Error: Cannot schedule menu window transition");
        }
    };
    showcase.Log("Use Alt/F10 for the menu bar and Shift+F10 for context menus. Output is shown in the Events window.");
    LWS::HighPrecisionTimer smokeExit(platform, [&] { platform.RequestQuit(); });
    if (smoke || snapshot)
    {
        smokeExit.SetDueTime(2000);
        smokeExit.SetRepeatInterval(0);
        smokeExit.Enable(true);
    }
    if (platform.RunMessageLoop() == LWS::LoopResult::Failed)
    {
        ReportShowcaseError("Message loop failed", !(smoke || snapshot));
        return 3;
    }
    if (snapshot)
        showcase.Snapshot();
    if ((smoke || snapshot) && !showcase.Verify())
        return 4;
    return 0;
}
