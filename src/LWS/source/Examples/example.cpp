#include <LWS/Cursor.hpp>
#include <LWS/Platform.hpp>
#include <LWS/Window.hpp>
#include <LLUtils/StringDefs.h>
#include <LLUtils/Colors.h>
#ifdef LWS_HAS_WIN32_BACKEND
    #include <LWS/Win32/Platform.hpp>
#endif

int main()
{
    using namespace LWS;

#ifdef LWS_HAS_WIN32_BACKEND
    if (Win32::BootstrapProcess() != Result::Success)
        return 1;
#endif
    PlatformContext platform;
    const PlatformConfig platformConfig{
#ifdef LWS_HAS_WIN32_BACKEND
        .backend = BackendId::Win32,
#else
        .backend = BackendId::Wayland,
#endif
    };
    if (platform.Init(platformConfig) != Result::Success)
        return 1;

    {
        Window win(platform);
        WindowConfig config;
        config.title = LLUTILS_TEXT("Hello LWS");
        config.clientSize = {800, 600};
        config.styles = WindowStyleFlags(WindowStyle::Caption | WindowStyle::CloseButton | WindowStyle::MaximizeButton |
                                         WindowStyle::MinimizeButton | WindowStyle::ResizableBorder);
        config.backgroundColor = LLUtils::Colors::Red;
        config.visible = true;

        auto connection = win.Listen(
            [&](const AnyEvent& event)
            {
                if (std::holds_alternative<EventWindowDestroyed>(event))
                    platform.RequestQuit();
                return EventResponse::Unhandled;
            });

        if (!connection.has_value() || win.Create(config) != Result::Success)
            return 1;

        if (platform.RunMessageLoop() == LWS::LoopResult::Failed)
            return 1;
        std::ignore = win.Destroy();
    }
    return platform.Shutdown() == Result::Success ? 0 : 1;
}
