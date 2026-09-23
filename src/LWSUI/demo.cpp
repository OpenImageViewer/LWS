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
    LWSUI::UIHost host(window);
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
    std::cout << "LWSUI controls: right-click text or Enable feature for commands; Shift+F10 also opens a menu. "
                 "Changes are printed below."
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
