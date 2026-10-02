#include <LWS/Platform.hpp>
#ifdef LWS_HAS_WIN32_BACKEND
    #include <LWS/Win32/Platform.hpp>
#endif
#include <LWSUI/Containers.hpp>
#include <LWSUI/UIHost.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

int main()
{
    try
    {
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
        LWS::Window window(platform);
        LWS::WindowConfig config;
        config.title = LWSUI::NativeText("LWSUI / Simple example");
        config.clientSize = {420, 500};
        config.clientSizeLimits.minimum = {240, 320};
        config.styles = LWS::WindowStyle::Caption | LWS::WindowStyle::CloseButton | LWS::WindowStyle::ResizableBorder;
        config.eraseBackground = false;
        check(window.Create(config), "Cannot create the window");
        LWSUI::UIHost host(window);
        host.SetRedrawOnResize(true);

        auto content = std::make_unique<LWSUI::StackPanel>();
        auto textBox1 = std::make_unique<LWSUI::TextBox>("first textbox", LWSUI::TextBoxMode::Multiline);
        auto textBox2 = std::make_unique<LWSUI::TextBox>("second textbox", LWSUI::TextBoxMode::Multiline);
        auto splitPanel = std::make_unique<LWSUI::SplitPanel>(std::move(textBox1), std::move(textBox2));
        // Leave room for the controls below, without a separate sizing wrapper.
        splitPanel->SetMaxHeight(400);
        content->Add(std::move(splitPanel));

        auto& name = content->Emplace<LWSUI::TextBox>();
        name.SetPlaceholder("Your name");
        auto& greeting = content->Emplace<LWSUI::Label>("Hello!");
        greeting.trimming = LWSUI::TextTrimming::Ellipsis;
        auto& clear = content->Emplace<LWSUI::Button>("Clear");
        auto panel = std::make_unique<LWSUI::Panel>(std::move(content));
        panel->padding = LWSUI::Insets{16};
        host.SetRoot(std::move(panel));

        // Retain these connections for as long as the callbacks should be active.
        auto edited = name.OnEdit.Connect([&](const std::string& text, LWSUI::EditPhase)
                                          { greeting.SetText(text.empty() ? "Hello!" : "Hello, " + text + "!"); });
        auto cleared = clear.OnClick.Connect(
            [&]
            {
                // Programmatic SetText does not emit OnEdit.
                name.SetText("");
                greeting.SetText("Hello!");
            });
        bool failed = false;
        auto errors = host.OnError.Connect(
            [&](const std::string& message)
            {
                std::cerr << "LWSUI example: " << message << '\n';
                failed = true;
                platform.RequestQuit();
            });
        auto closing = window.Listen(
            [&](const LWS::AnyEvent& event)
            {
                if (std::holds_alternative<LWS::EventCloseRequested>(event))
                {
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
        host.Update();
        const auto result = platform.RunMessageLoop();
        if (result == LWS::LoopResult::Failed)
            std::cerr << "LWSUI example: Message loop failed\n";
        return failed || result == LWS::LoopResult::Failed ? 1 : 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "LWSUI example: " << error.what() << '\n';
        return 1;
    }
}
