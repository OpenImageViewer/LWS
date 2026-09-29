#include <LWSUI/UIHost.hpp>
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
namespace
{
    template <class T>
    void PrintValue(const T& value)
    {
        if constexpr (std::is_same_v<T, std::string>)
            std::cout << std::quoted(value);
        else if constexpr (std::is_same_v<T, LLUtils::Color>)
        {
            char text[10];
            std::snprintf(text, sizeof(text), "#%02x%02x%02x%02x", value.R(), value.G(), value.B(), value.A());
            std::cout << std::quoted(text);
        }
        else
            std::cout << std::boolalpha << std::setprecision(17) << value;
    }
    template <class T>
    void Log(const std::string& control, const T& oldValue, const T& value, LWSUI::EditPhase phase)
    {
        std::cout << "{\"control\":" << std::quoted(control) << ",\"oldValue\":";
        PrintValue(oldValue);
        std::cout << ",\"newValue\":";
        PrintValue(value);
        std::cout << ",\"phase\":\""
                  << (phase == LWSUI::EditPhase::Preview  ? "preview"
                      : phase == LWSUI::EditPhase::Cancel ? "cancel"
                                                          : "commit")
                  << "\"}" << std::endl;
    }
    class DemoSubscriptions
    {
      public:

        template <class Event, class Callback>
        void Connect(Event& event, Callback callback)
        {
            connections_.push_back(std::make_shared<typename Event::Connection>(event.Connect(std::move(callback))));
        }
        template <class T>
        void Edits(LWSUI::Event<void(T, LWSUI::EditPhase)>& event, std::string name, std::remove_cvref_t<T> initial)
        {
            Connect(event,
                    [name = std::move(name), previous = initial, start = initial,
                     editing = false](T value, LWSUI::EditPhase phase) mutable
                    {
                        if (phase == LWSUI::EditPhase::Preview && !editing)
                        {
                            start = previous;
                            editing = true;
                        }
                        Log(name, phase == LWSUI::EditPhase::Commit && editing ? start : previous, value, phase);
                        previous = value;
                        if (phase != LWSUI::EditPhase::Preview)
                            editing = false;
                    });
        }
        template <class T>
        void Changes(LWSUI::Event<void(T)>& event, std::string name, std::remove_cvref_t<T> initial)
        {
            Connect(event,
                    [name = std::move(name), previous = initial](T value) mutable
                    {
                        Log(name, previous, value, LWSUI::EditPhase::Commit);
                        previous = value;
                    });
        }

      private:

        std::vector<std::shared_ptr<void>> connections_;
    };
    template <class T, class... Args>
    T& Row(LWSUI::StackPanel& parent, std::string label, Args&&... args)
    {
        auto editor = std::make_unique<T>(std::forward<Args>(args)...);
        auto& result = *editor;
        parent.Emplace<LWSUI::TreeView::Row>(std::make_unique<LWSUI::Label>(std::move(label)), std::move(editor));
        return result;
    }
    void Snapshot(LWSUI::UIHost& host)
    {
        auto bounds = host.Root()->Bounds();
        LWSUI::Canvas canvas;
        canvas.Begin(int(bounds.width), int(bounds.height));
        canvas.Fill(0, 0, bounds.width, bounds.height, host.Style().background);
        host.Root()->Render(canvas);
        auto pixels = canvas.End();
        std::ofstream output("controls.ppm", std::ios::binary);
        output << "P6\n" << pixels.width << " " << pixels.height << "\n255\n";
        for (unsigned y = 0; y < pixels.height; ++y)
            for (unsigned x = 0; x < pixels.width; ++x)
            {
                auto p = pixels.pixels.data() + y * pixels.rowPitch + x * 4;
                char rgb[] = {char(p[2]), char(p[1]), char(p[0])};
                output.write(rgb, 3);
            }
    }
}  // namespace
int main(int argc, char** argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    bool smoke = false, snapshot = false;
    for (int i = 1; i < argc; ++i)
    {
        std::string argument = argv[i];
        smoke |= argument == "--smoke";
        snapshot |= argument == "--snapshot";
    }
#ifdef LWS_HAS_WIN32_BACKEND
    if (LWS::Win32::BootstrapProcess() != LWS::Result::Success)
        return 1;
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
        return 1;
    LWS::Window window(platform);
    LWS::WindowConfig config;
    config.title = LWSUI::NativeText("LWSUI controls — tree and events");
    config.clientSize = {820, 760};
    config.visible = true;
    config.styles = LWS::WindowStyle::Caption | LWS::WindowStyle::CloseButton | LWS::WindowStyle::ResizableBorder;
    if (window.Create(config) != LWS::Result::Success)
        return 2;
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
                if (uiHost->MainMenuDock() == LWSUI::MenuDock::Floating && bar &&
                    bar->Bounds().Contains(float(button->position.x), float(button->position.y)))
                {
                    menuDrag.armed = true;
                    menuDrag.pointerX = float(button->position.x);
                    menuDrag.pointerY = float(button->position.y);
                    menuDrag.barX = bar->Bounds().x;
                    menuDrag.barY = bar->Bounds().y;
                }
                return LWS::EventResponse::Unhandled;
            }
            if (const auto* move = std::get_if<LWS::EventMouseMove>(&event); move && menuDrag.armed &&
                uiHost->MainMenuDock() == LWSUI::MenuDock::Floating)
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
    LWSUI::UIHost host(window);
    uiHost = &host;
    DemoSubscriptions subscriptions;
    auto tree = std::make_unique<LWSUI::TreeView>();
    auto& general = tree->Emplace<LWSUI::TreeView::Branch>("General");
    auto& palette = Row<LWSUI::Button>(general.Content(), "Palette", "Dark");
    auto paletteMenu = [&host](LWSUI::Control& owner, const LWSUI::ContextMenuRequest&)
    {
        std::vector<LWSUI::ContextMenuItem> items;
        for (auto preset : {LWSUI::ThemePreset::Dark, LWSUI::ThemePreset::Light, LWSUI::ThemePreset::Warm})
        {
            const std::string name = preset == LWSUI::ThemePreset::Dark    ? "Dark"
                                     : preset == LWSUI::ThemePreset::Light ? "Light"
                                                                           : "Warm";
            items.push_back({name, "",
                             [&host, preset, name](LWSUI::Control& button)
                             {
                                 host.ClosePopup(LWSUI::EditPhase::Cancel);
                                 host.SetTheme(LWSUI::MakeTheme(preset));
                                 static_cast<LWSUI::Button&>(button).SetText(name);
                             },
                             true, static_cast<LWSUI::Button&>(owner).Text() == name});
        }
        return items;
    };
    palette.SetContextMenuProvider(paletteMenu);
    subscriptions.Connect(palette.OnClick,
                          [&host, &palette, paletteMenu]
                          {
                              const auto b = palette.Bounds();
                              LWSUI::ContextMenuRequest request{palette.Handle(), b.x, b.y + b.height, true};
                              host.ShowContextMenu(palette, request, paletteMenu(palette, request));
                          });
    auto& text = Row<LWSUI::TextBox>(general.Content(), "Display name", "Edit text here");
    subscriptions.Edits(text.OnEdit, "displayName", text.Text());
    auto& enabled = Row<LWSUI::CheckBox>(general.Content(), "Enable feature");
    subscriptions.Changes(enabled.OnChange, "enabled", enabled.Value());
    enabled.SetContextMenuProvider(
        [](LWSUI::Control& owner, const LWSUI::ContextMenuRequest&)
        {
            auto& checkbox = static_cast<LWSUI::CheckBox&>(owner);
            return std::vector<LWSUI::ContextMenuItem>{{"Enable feature", "",
                                                        [](LWSUI::Control& c)
                                                        {
                                                            auto& check = static_cast<LWSUI::CheckBox&>(c);
                                                            check.SetValue(!check.Value());
                                                            check.OnChange.Raise(check.Value());
                                                        },
                                                        true, checkbox.Value()},
                                                       LWSUI::ContextMenuItem::Separator(),
                                                       {"Reset to disabled", "",
                                                        [](LWSUI::Control& c)
                                                        {
                                                            auto& check = static_cast<LWSUI::CheckBox&>(c);
                                                            check.SetValue(false);
                                                            check.OnChange.Raise(false);
                                                        },
                                                        checkbox.Value()}};
        });
    auto& appearance = tree->Emplace<LWSUI::TreeView::Branch>("Appearance");
    auto& combo = Row<LWSUI::ComboBox>(
        appearance.Content(), "Theme",
        std::vector<LWSUI::Choice>{{"Follow system", "system"}, {"Dark", "dark"}, {"Light", "light"}});
    subscriptions.Changes(combo.OnChange, "theme", combo.Value());
    auto& advanced = appearance.Content().Emplace<LWSUI::TreeView::Branch>("Advanced display");
    auto& integer = Row<LWSUI::NumericEdit<int64_t>>(advanced.Content(), "Refresh rate",
                                                     LWSUI::NumericSpec<int64_t>{1, 240, 1});
    integer.SetValue(60);
    subscriptions.Edits(integer.OnEdit, "refreshRate", integer.Value());
    auto& decimal = Row<LWSUI::NumericEdit<double>>(advanced.Content(), "Content scale",
                                                    LWSUI::NumericSpec<double>{.5, 4, .1});
    decimal.SetValue(1);
    subscriptions.Edits(decimal.OnEdit, "scale", decimal.Value());
    auto& radio = Row<LWSUI::RadioGroup>(
        advanced.Content(), "Rendering quality",
        std::vector<LWSUI::Choice>{{"Fast", "fast"}, {"Balanced", "balanced"}, {"High quality", "high"}});
    radio.SetValue("balanced");
    subscriptions.Changes(radio.OnChange, "quality", radio.Value());
    auto& color = Row<LWSUI::ColorPicker>(appearance.Content(), "Accent color");
    color.SetValue(LLUtils::Color{uint32_t{0x66b7ffff}});
    subscriptions.Edits(color.OnEdit, "accent", color.Value());
    auto& slider = Row<LWSUI::Slider>(appearance.Content(), "Opacity");
    slider.SetValue(1);
    subscriptions.Edits(slider.OnEdit, "opacity", slider.Value());
    auto& files = tree->Emplace<LWSUI::TreeView::Branch>("Files");
    auto& file = Row<LWSUI::FilePicker>(files.Content(), "Image file");
    subscriptions.Edits(file.OnEdit, "file", file.Value());
    for (auto* branch : {&general, &appearance, &advanced, &files})
        subscriptions.Changes(branch->OnExpanded, "tree/" + branch->Header().Text(), branch->Expanded());
    subscriptions.Connect(host.OnError,
                          [](const std::string& message) { std::cerr << "UI error: " << message << std::endl; });
    auto scroll = std::make_unique<LWSUI::ScrollView>();
    scroll->overlayScrollBar = true;
    scroll->SetContent(std::move(tree));
    host.SetRoot(std::move(scroll));
    struct MenuState
    {
        LWSUI::ThemePreset preset = LWSUI::ThemePreset::Dark;
        LWSUI::MenuDock dock = LWSUI::MenuDock::Top;
        bool vertical = false, detached = false;
    };
    struct DetachedMenu
    {
        std::unique_ptr<LWS::Window> window;
        std::unique_ptr<LWSUI::UIHost> host;
        LWS::EventConnection dragListener;
        LWS::EventConnection closeListener;
    };
    MenuState menuState;
    std::unique_ptr<DetachedMenu> detached;
    auto lifetime = std::make_shared<int>(0);
    bool transitionPending = false;
    const auto command = [](std::string name)
    {
        return [name = std::move(name)]
        { std::cout << "{\"command\":" << std::quoted(name) << "}" << std::endl; };
    };
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
        const auto dock = vertical ? (onSide() ? menuState.dock : LWSUI::MenuDock::Left)
                                   : (menuState.dock == LWSUI::MenuDock::Top ||
                                              menuState.dock == LWSUI::MenuDock::Bottom
                                          ? menuState.dock
                                          : LWSUI::MenuDock::Top);
        applyPlacement(dock, vertical);
    };
    const auto buildItems = [&](const MenuState& state)
    {
        const auto theme = [&host, &detached, &refresh, &state, &menuState](LWSUI::ThemePreset preset, const char* label)
        {
            return LWSUI::MenuItem{.label = label,
                                   .action = [&host, &detached, &refresh, &menuState, preset]
                                   {
                                       menuState.preset = preset;
                                       const auto selectedTheme = LWSUI::MakeTheme(preset);
                                       host.SetTheme(selectedTheme);
                                       if (detached)
                                           detached->host->SetTheme(selectedTheme);
                                       refresh();
                                   },
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
             {LWSUI::MenuItem{.label = "&New", .shortcut = "Ctrl+N", .action = command("file/new")},
              LWSUI::MenuItem{.label = "&Open...", .shortcut = "Ctrl+O", .action = command("file/open")},
              LWSUI::MenuItem{.label = "&Save", .shortcut = "Ctrl+S", .action = command("file/save")},
              LWSUI::MenuItem::Separator(),
              LWSUI::MenuItem{.label = "&Recent",
                              .submenu = {LWSUI::MenuItem{.label = "&Project",
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
              LWSUI::MenuItem{.label = "&Redo", .shortcut = "Ctrl+Y", .enabled = false},
              LWSUI::MenuItem::Separator(),
              LWSUI::MenuItem{.label = "Cu&t", .shortcut = "Ctrl+X", .action = command("edit/cut")},
              LWSUI::MenuItem{.label = "&Copy", .shortcut = "Ctrl+C", .action = command("edit/copy")},
              LWSUI::MenuItem{.label = "&Paste", .shortcut = "Ctrl+V", .action = command("edit/paste")}}},
            {"&View",
             {LWSUI::MenuItem{.label = "&Theme",
                              .submenu = {theme(LWSUI::ThemePreset::Dark, "&Dark"),
                                          theme(LWSUI::ThemePreset::Light, "&Light"),
                                          theme(LWSUI::ThemePreset::Warm, "&Warm")}},
              LWSUI::MenuItem{.label = "Menu b&ar",
                              .submenu = {LWSUI::MenuItem{.label = "&Detached",
                                                          .action = [&setDetached, &state]
                                                          { setDetached(!state.detached); },
                                                          .checked = state.detached},
                                          LWSUI::MenuItem{.label = "&Dock",
                                                          .enabled = !state.detached,
                                                          .submenu = {dockItem(LWSUI::MenuDock::Top, "&Top"),
                                                                      dockItem(LWSUI::MenuDock::Bottom, "&Bottom"),
                                                                      dockItem(LWSUI::MenuDock::Left, "&Left"),
                                                                      dockItem(LWSUI::MenuDock::Right, "&Right"),
                                                                      dockItem(LWSUI::MenuDock::Floating, "&Floating")}},
                                          LWSUI::MenuItem{.label = "&Vertical",
                                                          .action = [&orientBar, &state]
                                                          { orientBar(!state.vertical); },
                                                          .checked = state.vertical}}}}},
            {"&Help", {LWSUI::MenuItem{.label = "&About", .action = command("help/about")}}}};
        if (state.detached || state.dock == LWSUI::MenuDock::Floating)
            items.insert(items.begin(), {"⠿", {}, false});
        return items;
    };
    refresh = [&host, &detached, &menuState, &buildItems]
    {
        if (auto* bar = host.MainMenu())
            bar->SetItems(buildItems(menuState));
        if (detached)
            if (auto* bar = detached->host->MainMenu())
            {
                bar->SetItems(buildItems(menuState));
                const float span = bar->NaturalSpan();
                const float thickness = bar->Thickness();
                constexpr float clientPadding = 8.f;
                const auto resized = detached->window->RequestPlacement(
                    LWS::WindowPlacementRequest{
                        .clientSize = menuState.vertical
                                          ? LWS::LogicalSize{int32_t(thickness + clientPadding),
                                                             int32_t(span + clientPadding)}
                                          : LWS::LogicalSize{int32_t(span + clientPadding),
                                                             int32_t(thickness + clientPadding)}});
                if (resized != LWS::Result::Success)
                    std::cerr << "Cannot resize detached menu window" << std::endl;
            }
    };
    host.SetMainMenu(std::make_unique<LWSUI::MenuBar>(buildItems(menuState)), menuState.dock);
    setDetached = [&](bool detach)
    {
        if (transitionPending || menuState.detached == detach)
            return;
        transitionPending = true;
        const auto weakLifetime = std::weak_ptr(lifetime);
        if (platform.PostTask([&, weakLifetime, detach]
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
                                                ? LWS::LogicalSize{int32_t(thickness + 8.f), int32_t(span + 64.f)}
                                                : LWS::LogicalSize{int32_t(span + 64.f), int32_t(thickness + 8.f)};
                detachedConfig.styles = LWS::WindowStyle::NoStyle;
                detachedConfig.backgroundColor = LWSUI::MakeTheme(menuState.preset).background;
                if (candidate->window->Create(detachedConfig) != LWS::Result::Success)
                {
                    std::cerr << "Cannot create detached menu window" << std::endl;
                    return;
                }
                if (candidate->window->SetWindowStyles(LWS::WindowStyle::NoStyle) != LWS::Result::Success)
                {
                    std::cerr << "Cannot remove detached menu window decorations" << std::endl;
                    return;
                }
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
                        return detachedWindow->BeginWindowDrag(LWS::WindowDragOperation::Move) == LWS::Result::Success
                                   ? LWS::EventResponse::Handled
                                   : LWS::EventResponse::Unhandled;
                    });
                if (!dragListener)
                {
                    std::cerr << "Cannot listen for detached menu dragging" << std::endl;
                    return;
                }
                candidate->dragListener = std::move(*dragListener);
                candidate->host = std::make_unique<LWSUI::UIHost>(
                    *candidate->window, LWSUI::MakeTheme(menuState.preset));
                menuState.detached = true;
                auto bar = std::make_unique<LWSUI::MenuBar>(buildItems(menuState));
                bar->orientation = menuState.vertical ? LWSUI::Orientation::Vertical : LWSUI::Orientation::Horizontal;
                candidate->host->SetMainMenu(std::move(bar), LWSUI::MenuDock::Floating);
                const auto* detachedBar = candidate->host->MainMenu();
                const float detachedSpan = detachedBar->NaturalSpan();
                const float detachedThickness = detachedBar->Thickness();
                constexpr float clientPadding = 8.f;
                const auto resized = detachedWindow->RequestPlacement(
                    LWS::WindowPlacementRequest{
                        .clientSize = menuState.vertical
                                          ? LWS::LogicalSize{int32_t(detachedThickness + clientPadding),
                                                             int32_t(detachedSpan + clientPadding)}
                                          : LWS::LogicalSize{int32_t(detachedSpan + clientPadding),
                                                             int32_t(detachedThickness + clientPadding)}});
                if (resized != LWS::Result::Success)
                {
                    menuState.detached = false;
                    std::cerr << "Cannot size detached menu window" << std::endl;
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
                    std::cerr << "Cannot listen to detached menu window" << std::endl;
                    return;
                }
                candidate->closeListener = std::move(*closeListener);
                if (detachedWindow->SetVisible(true) != LWS::Result::Success)
                {
                    menuState.detached = false;
                    std::cerr << "Cannot show detached menu window" << std::endl;
                    return;
                }
                detached = std::move(candidate);
                host.ClearMainMenu();
                refresh();
                return;
            }
            menuState.detached = false;
            auto bar = std::make_unique<LWSUI::MenuBar>(buildItems(menuState));
            bar->orientation = menuState.vertical ? LWSUI::Orientation::Vertical : LWSUI::Orientation::Horizontal;
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
            std::cerr << "Cannot schedule menu window transition" << std::endl;
        }
    };
    std::cout << "LWSUI controls: menu bar via Alt/F10; right-click text or Enable feature for commands; "
                 "Shift+F10 also opens a menu. Changes are printed below."
              << std::endl;
    auto closed = window.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventWindowDestroyed>(event))
                platform.RequestQuit();
            return LWS::EventResponse::Unhandled;
        });
    if (!closed)
        return 3;
    LWS::HighPrecisionTimer smokeExit(platform, [&] { platform.RequestQuit(); });
    if (smoke || snapshot)
    {
        smokeExit.SetDueTime(2000);
        smokeExit.SetRepeatInterval(0);
        smokeExit.Enable(true);
    }
    if (platform.RunMessageLoop() == LWS::LoopResult::Failed)
        return 3;
    if (snapshot)
        Snapshot(host);
}
