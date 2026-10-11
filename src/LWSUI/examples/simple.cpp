#include <LWS/Platform.hpp>
#ifdef LWS_HAS_WIN32_BACKEND
    #include <LWS/Win32/Platform.hpp>
#endif
#include <LWSUI/Containers.hpp>
#include <LWSUI/UIHost.hpp>
#include <chrono>
#include <deque>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace
{
    void ReportError(const std::string& message, bool interactive)
    {
#ifdef LWS_HAS_WIN32_BACKEND
        OutputDebugStringA(("LWSUI example: " + message + "\n").c_str());
        if (interactive)
            MessageBoxW(nullptr, LWSUI::NativeText(message).c_str(), L"LWSUI Simple example", MB_OK | MB_ICONERROR);
#else
        std::cerr << "LWSUI example: " << message << '\n';
        (void) interactive;
#endif
    }
    const char* PhaseName(LWSUI::EditPhase phase)
    {
        switch (phase)
        {
            case LWSUI::EditPhase::Preview:
                return "Preview";
            case LWSUI::EditPhase::Commit:
                return "Commit";
            default:
                return "Cancel";
        }
    }
    std::string LogValue(std::string_view value)
    {
        std::string result;
        for (char c : value)
        {
            if (c == '\n')
                result += "\\n";
            else if (c == '\r')
                result += "\\r";
            else
                result += c;
        }
        return result;
    }
}  // namespace

int main(int argc, char** argv)
{
    const bool smoke = argc == 2 && std::string_view(argv[1]) == "--smoke";
    try
    {
        if (argc > 1 && !smoke)
            throw std::invalid_argument("Usage: LWSUISimpleExample [--smoke]");
        const auto check = [](LWS::Result result, const char* message)
        {
            if (result != LWS::Result::Success)
                throw std::runtime_error(message);
        };
#ifdef LWS_HAS_WIN32_BACKEND
        check(LWS::Win32::BootstrapProcess(), "Cannot initialize the Windows process");
#endif
        LWS::PlatformContext platform;
        const LWS::PlatformConfig platformConfig{
#ifdef LWS_HAS_WIN32_BACKEND
            .backend = LWS::BackendId::Win32,
#else
            .backend = LWS::BackendId::Wayland,
#endif
        };
        check(platform.Init(platformConfig), "Cannot initialize the platform");

        // The platform outlives the window, which outlives its host and controls.
        const auto theme = LWSUI::MakeTheme();
        LWS::Window window(platform);
        LWS::WindowConfig config;
        config.title = LWSUI::NativeText("LWSUI / Simple example");
        config.clientSize = {420, 640};
        config.clientSizeLimits.minimum = {320, 480};
        config.backgroundColor = theme.background;
        config.styles = LWS::WindowStyle::Caption | LWS::WindowStyle::CloseButton | LWS::WindowStyle::ResizableBorder;
        config.eraseBackground = false;
        check(window.Create(config), "Cannot create the window");
        LWSUI::UIHost host(window);
        host.SetRedrawOnResize(true);

        auto controls = std::make_unique<LWSUI::DockPanel>();
        controls->gap = 8;
        auto actions = std::make_unique<LWSUI::StackPanel>();
        auto& name = actions->Emplace<LWSUI::TextBox>();
        name.SetPlaceholder("Your name");
        auto& greeting = actions->Emplace<LWSUI::Label>("Hello!");
        greeting.trimming = LWSUI::TextTrimming::Ellipsis;
        auto& clear = actions->Emplace<LWSUI::Button>("Clear");
        controls->AddDocked(std::move(actions), LWSUI::Dock::Bottom);
        auto first = std::make_unique<LWSUI::TextBox>("first textbox", LWSUI::TextBoxMode::Multiline);
        auto second = std::make_unique<LWSUI::TextBox>("second textbox", LWSUI::TextBoxMode::Multiline);
        auto& firstText = *first;
        auto& secondText = *second;
        auto textSplit = std::make_unique<LWSUI::SplitPanel>(std::move(first), std::move(second));
        auto& innerSplit = *textSplit;
        innerSplit.SetMinimumSizes(45, 45);
        controls->AddDocked(std::move(textSplit), LWSUI::Dock::Fill);
        auto upper = std::make_unique<LWSUI::Panel>(std::move(controls));
        upper->padding = LWSUI::Insets{8};

        auto events = std::make_unique<LWSUI::DockPanel>();
        events->gap = 6;
        auto toolbar = std::make_unique<LWSUI::FlowPanel>();
        auto title = std::make_unique<LWSUI::Label>("Events / latest 200");
        title->fontRole = LWSUI::Control::FontRole::Heading;
        toolbar->AddItem(std::move(title), true);
        auto clearLogButton = std::make_unique<LWSUI::Button>("Clear log");
        clearLogButton->sizeToContent = true;
        auto& clearLog = *clearLogButton;
        toolbar->AddItem(std::move(clearLogButton));
        events->AddDocked(std::move(toolbar), LWSUI::Dock::Top);
        auto viewer = std::make_unique<LWSUI::TextBox>("", LWSUI::TextBoxMode::Multiline);
        auto& logText = *viewer;
        logText.SetReadOnly(true);
        logText.fontRole = LWSUI::Control::FontRole::Small;
        logText.SetPlaceholder("Control events appear here. Select text to copy it.");
        events->AddDocked(std::move(viewer), LWSUI::Dock::Fill);
        auto eventPanel = std::make_unique<LWSUI::Panel>(std::move(events));
        eventPanel->padding = LWSUI::Insets{8};
        eventPanel->border = LWSUI::Insets{1};
        auto split = std::make_unique<LWSUI::SplitPanel>(std::move(upper), std::move(eventPanel));
        auto& outerSplit = *split;
        outerSplit.SetDefaultTrailingSize(180, .45f);
        outerSplit.SetMinimumSizes(180, 120);
        auto root = std::make_unique<LWSUI::Panel>(std::move(split));
        root->padding = LWSUI::Insets{12};
        host.SetRoot(std::move(root));

        std::deque<std::string> entries;
        size_t sequence = 0;
        const auto log = [&](std::string message)
        {
            entries.push_back(std::to_string(++sequence) + "  " + std::move(message));
            if (entries.size() > 200)
                entries.pop_front();
            std::string text;
            for (const auto& entry : entries)
                text += entry + '\n';
            // Silent assignment updates the viewer without generating another edit event.
            logText.SetText(std::move(text));
        };
        LWSUI::ConnectionSet connections;
        const auto watch = [&](LWSUI::TextBox& field, std::string source)
        {
            connections.Connect(field.OnEdit, [&, source](const std::string& value, LWSUI::EditPhase phase)
                                { log(source + "." + PhaseName(phase) + "  " + LogValue(value)); });
        };
        watch(firstText, "First textbox");
        watch(secondText, "Second textbox");
        watch(name, "Name");
        connections.Connect(name.OnEdit, [&](const std::string& value, LWSUI::EditPhase)
                            { greeting.SetText(value.empty() ? "Hello!" : "Hello, " + value + "!"); });
        connections.Connect(clear.OnClick,
                            [&]
                            {
                                name.SetText("");
                                greeting.SetText("Hello!");
                                log("Clear.Click  Name and greeting reset");
                            });
        connections.Connect(clearLog.OnClick,
                            [&]
                            {
                                entries.clear();
                                logText.SetText("");
                            });
        const auto watchSplit = [&](LWSUI::SplitPanel& panel, std::string source)
        {
            connections.Connect(panel.OnEdit, [&, source](float value, LWSUI::EditPhase phase)
                                { log(source + "." + PhaseName(phase) + "  ratio=" + std::to_string(value)); });
        };
        watchSplit(innerSplit, "Textbox divider");
        watchSplit(outerSplit, "Events divider");
        bool failed = false;
        connections.Connect(host.OnError,
                            [&](const std::string& message)
                            {
                                failed = true;
                                log("Host.Error  " + message);
                                ReportError(message, !smoke);
                                platform.RequestQuit();
                            });
        auto closing = window.Listen(
            [&](const LWS::AnyEvent& event)
            {
                if (std::holds_alternative<LWS::EventClientAreaSizeChanged>(event))
                {
                    const auto size = window.GetClientAreaMetrics().logical;
                    log("Window.Resize  " + std::to_string(size.x) + " x " + std::to_string(size.y));
                }
                if (std::holds_alternative<LWS::EventCloseRequested>(event))
                {
                    log("Window.Close");
                    platform.RequestQuit();
                    return LWS::EventResponse::Handled;
                }
                if (std::holds_alternative<LWS::EventWindowDestroyed>(event))
                    platform.RequestQuit();
                return LWS::EventResponse::Unhandled;
            });
        if (!closing)
            throw std::runtime_error("Cannot register the window listener");
        check(window.SetVisible(true), "Cannot show the window");
        log("Window.Show  Light theme");
        host.Update();

        if (smoke)
        {
            const auto require = [](bool condition, const char* message)
            {
                if (!condition)
                    throw std::runtime_error(message);
            };
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (!window.IsConfigured() && std::chrono::steady_clock::now() < deadline)
            {
                require(platform.ProcessMessages() == LWS::LoopResult::Continue, "Smoke message processing failed");
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            require(window.IsConfigured(), "Smoke window was not configured");
            host.Update();
            const auto light = LWSUI::MakeTheme(LWSUI::ThemePreset::Light);
            require(LWSUI::DefaultThemePreset == LWSUI::ThemePreset::Light &&
                        LWSUI::Theme{}.background == light.background && host.Style().background == light.background &&
                        theme.surface == light.surface,
                    "Default theme is not Light");
            const auto edit = [&](LWSUI::TextBox& field, std::string value, LWSUI::EditPhase phase)
            {
                require(host.Focus(&field), "Cannot focus smoke editor");
                require(field.Dispatch({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::A, .control = true}),
                        "Cannot select smoke text");
                require(field.Dispatch({.kind = LWSUI::InputKind::Text, .text = std::move(value)}),
                        "Cannot edit smoke text");
                require(field.Finish(phase), "Cannot finish smoke edit");
                require(host.IsFocused(field), "Logging stole editor focus");
            };
            edit(name, "Ada", LWSUI::EditPhase::Commit);
            require(greeting.Text() == "Hello, Ada!", "Greeting was not updated");
            edit(firstText, "first draft", LWSUI::EditPhase::Cancel);
            edit(secondText, "second draft", LWSUI::EditPhase::Commit);
            require(firstText.Text() == "first textbox" && secondText.Text() == "second draft",
                    "Textbox transactions failed");
            const auto b = innerSplit.Divider().Bounds();
            require(host.Route({.kind = LWSUI::InputKind::Down, .x = b.x + b.width / 2, .y = b.y + b.height / 2}),
                    "Cannot start smoke divider drag");
            require(host.Route({.kind = LWSUI::InputKind::Move, .x = b.x + b.width / 2, .y = b.y + b.height / 2 + 10}),
                    "Cannot drag smoke divider");
            require(host.Route({.kind = LWSUI::InputKind::Up, .x = b.x + b.width / 2, .y = b.y + b.height / 2 + 10}),
                    "Cannot finish smoke divider drag");
            clear.OnClick.Raise();
            require(name.Text().empty() && greeting.Text() == "Hello!", "Clear did not reset controls");
            for (auto expected : {"Name.Preview", "Name.Commit", "First textbox.Cancel", "Second textbox.Commit",
                                  "Textbox divider.Commit", "Clear.Click", "Window.Show"})
                require(logText.Text().find(expected) != std::string::npos, "An expected event is missing");
            const auto before = entries.size();
            name.SetText("Silent");
            require(entries.size() == before, "Programmatic assignment published an event");
            for (int i = 0; i < 205; ++i)
                log("Smoke.Entry " + std::to_string(i));
            require(entries.size() == 200 && logText.Text().find("Smoke.Entry 0\n") == std::string::npos,
                    "Event retention is not bounded");
            clearLog.OnClick.Raise();
            require(entries.empty() && logText.Text().empty() && name.Text() == "Silent",
                    "Clear log changed the controls or failed");
            require(logText.Dispatch({.kind = LWSUI::InputKind::Text, .text = "ignored"}) && logText.Text().empty(),
                    "Event viewer is editable");
            check(window.RequestPlacement({.clientSize = LWS::LogicalSize{320, 480}}), "Cannot resize smoke window");
            for (int i = 0; i < 20; ++i)
            {
                require(platform.ProcessMessages() == LWS::LoopResult::Continue, "Cannot process smoke resize");
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            host.Update();
            require(logText.Bounds().height > 0 && clear.Bounds().height > 0, "Controls disappeared at minimum size");
            require(logText.Text().find("Window.Resize") != std::string::npos, "Window resize was not logged");
            check(window.Destroy(), "Cannot destroy smoke window");
            return failed ? 1 : 0;
        }
        const auto result = platform.RunMessageLoop();
        if (result == LWS::LoopResult::Failed)
            ReportError("Message loop failed", true);
        return failed || result == LWS::LoopResult::Failed ? 1 : 0;
    }
    catch (const std::exception& error)
    {
        ReportError(error.what(), !smoke);
        return 1;
    }
}
