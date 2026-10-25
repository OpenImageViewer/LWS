#ifdef LWS_PLATFORM_WAYLAND

    #include <catch2/catch_test_macros.hpp>

    #include <LWS/Platform.hpp>
    #include <LWS/Timer.hpp>
    #include <LWS/Wayland/WindowExtensions.hpp>
    #include <LWS/Window.hpp>
    #include <LWS/TextClipboard.hpp>
    #include <LWS/FileDialog.hpp>
    #include <LWS/source/Wayland/internal/PlatformState.hpp>
    #include <LWS/source/Wayland/internal/KeyCodeLinux.hpp>
    #include <LWS/source/Wayland/internal/WindowBackendWayland.hpp>
    #include <LWS/source/internal/WindowBackendAccess.hpp>
    #include <sys/mman.h>
    #include <unistd.h>
    #include <cstring>

    #include <array>
    #include <chrono>
    #include <cstdlib>
    #include <ranges>
    #include <thread>
    #include <variant>
    #include <vector>

namespace
{
    bool Initialize(LWS::PlatformContext& context)
    {
        const LWS::Result result = context.Init({.backend = LWS::BackendId::Wayland});
        if (result != LWS::Result::Success)
            SKIP("A Wayland compositor is not available");
        return true;
    }
}  // namespace

namespace LWS::internal
{
    struct WaylandTextInputTestAccess
    {
        static void Exercise(Window& owner)
        {
            auto& backend = *static_cast<WindowBackendWayland*>(WindowBackendAccess::Get(owner));
            WaylandPlatformState platform(owner.GetPlatformContext());
            WaylandSeatController seat(platform);
            struct Cleanup
            {
                WaylandSeatController& seat;
                WindowBackendWayland& backend;
                ~Cleanup()
                {
                    seat.windowRemoved(backend);
                    seat.reset();
                }
            } cleanup{seat, backend};
            auto* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
            REQUIRE(context);
            xkb_rule_names names{};
            names.layout = "us";
            names.variant = "intl";
            auto* keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
            REQUIRE(keymap);
            char* serialized = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
            REQUIRE(serialized);
            const std::string keymapText(serialized);
            free(serialized);
            xkb_keymap_unref(keymap);
            xkb_context_unref(context);
            auto load = [&](uint32_t format)
            {
                const int fd = memfd_create("lws-input-test", MFD_CLOEXEC);
                REQUIRE(fd >= 0);
                REQUIRE(write(fd, keymapText.c_str(), keymapText.size() + 1) == ssize_t(keymapText.size() + 1));
                WaylandSeatController::keyboardKeymap(&seat, nullptr, format, fd, uint32_t(keymapText.size() + 1));
            };
            load(WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1);
            REQUIRE(seat.fXkbState);
            seat.fKeyboardWindow = &backend;
            std::string text;
            auto listener = owner.Listen(
                [&](const AnyEvent& event)
                {
                    if (const auto* value = std::get_if<EventTextInput>(&event))
                        text += value->text;
                    return EventResponse::Unhandled;
                });
            REQUIRE(listener.has_value());
            auto press = [&](uint32_t key)
            {
                WaylandSeatController::keyboardKey(&seat, nullptr, 1, 0, key, WL_KEYBOARD_KEY_STATE_PRESSED);
                WaylandSeatController::keyboardKey(&seat, nullptr, 2, 0, key, WL_KEYBOARD_KEY_STATE_RELEASED);
            };
            press(KEY_A);
            press(KEY_APOSTROPHE);
            press(KEY_E);
            REQUIRE(text == "a\xc3\xa9");
            auto nested = owner.Listen(
                [&](const AnyEvent& event)
                {
                    if (const auto* key = std::get_if<EventKeyDown>(&event); key && key->key == KeyCode::O)
                    {
                        WaylandSeatController::keyboardKey(&seat, nullptr, 3, 0, KEY_O, WL_KEYBOARD_KEY_STATE_RELEASED);
                        WaylandSeatController::keyboardModifiers(&seat, nullptr, 4, 0, 0, 0, 0);
                    }
                    return EventResponse::Unhandled;
                });
            REQUIRE(nested.has_value());
            const auto control = xkb_keymap_mod_get_index(seat.fXkbKeymap, XKB_MOD_NAME_CTRL);
            WaylandSeatController::keyboardModifiers(&seat, nullptr, 5, 1u << control, 0, 0, 0);
            press(KEY_O);
            REQUIRE(text == "a\xc3\xa9");
            nested->Disconnect();

            WaylandSeatController::keyboardKey(&seat, nullptr, 6, 0, KEY_LEFTCTRL, WL_KEYBOARD_KEY_STATE_PRESSED);
            REQUIRE(seat.isKeyPressed(KeyCode::Control));
            load(WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1);
            REQUIRE(seat.isKeyPressed(KeyCode::Control));
            load(WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP);
            REQUIRE(seat.isKeyPressed(KeyCode::Control));
            WaylandSeatController::keyboardKey(&seat, nullptr, 7, 0, KEY_LEFTCTRL, WL_KEYBOARD_KEY_STATE_RELEASED);
            REQUIRE_FALSE(seat.isKeyPressed(KeyCode::Control));
            load(WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1);

            auto* surface = static_cast<wl_surface*>(backend.surface());
            platform.registerWindow(surface, backend);
            uint32_t heldKeys[]{KEY_LEFTCTRL, KEY_102ND};
            wl_array held{sizeof(heldKeys), sizeof(heldKeys), heldKeys};
            WaylandSeatController::keyboardEnter(&seat, nullptr, 8, surface, &held);
            REQUIRE(seat.isKeyPressed(KeyCode::Control));
            REQUIRE(seat.fPressedScanCodes.contains(KEY_102ND));
            REQUIRE_FALSE(seat.isKeyPressed(KeyCode::Unknown));
            WaylandSeatController::keyboardLeave(&seat, nullptr, 9, surface);
            REQUIRE(seat.fPressedScanCodes.empty());
            platform.unregisterWindow(surface);
            seat.fKeyboardWindow = &backend;

            seat.initialize();
            seat.fKeyRepeatRate = 25;
            seat.fKeyRepeatDelay = 500;
            const auto beforeRepeat = text;
            WaylandSeatController::keyboardKey(&seat, nullptr, 10, 0, KEY_A, WL_KEYBOARD_KEY_STATE_PRESSED);
            REQUIRE(seat.fRepeatingScanCode == KEY_A);
            WaylandSeatController::keyboardKey(&seat, nullptr, 11, 0, KEY_102ND, WL_KEYBOARD_KEY_STATE_PRESSED);
            REQUIRE(seat.fRepeatingScanCode == KEY_102ND);
            seat.dispatchKeyRepeats(2);
            REQUIRE(text == beforeRepeat + "a" + std::string(3, '\\'));
            WaylandSeatController::keyboardKey(&seat, nullptr, 12, 0, KEY_A, WL_KEYBOARD_KEY_STATE_RELEASED);
            REQUIRE(seat.fRepeatingScanCode == KEY_102ND);
            WaylandSeatController::keyboardKey(&seat, nullptr, 13, 0, KEY_102ND, WL_KEYBOARD_KEY_STATE_RELEASED);
            REQUIRE_FALSE(seat.fRepeatingScanCode.has_value());
            seat.dispatchKeyRepeats(2);
            REQUIRE(text == beforeRepeat + "a" + std::string(3, '\\'));
            text = beforeRepeat;
            press(KEY_APOSTROPHE);
            seat.windowRemoved(backend);
            REQUIRE(xkb_compose_state_get_status(seat.fComposeState) == XKB_COMPOSE_NOTHING);
            seat.fKeyboardWindow = &backend;
            load(WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP);
            press(KEY_E);
            REQUIRE(text == "a\xc3\xa9");
            REQUIRE(seat.fXkbState == nullptr);
            load(WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1);
            xkb_compose_state_unref(seat.fComposeState);
            seat.fComposeState = nullptr;
            xkb_compose_table_unref(seat.fComposeTable);
            seat.fComposeTable = nullptr;
            const std::string longResult(200, 'x');
            const auto rules = "<a> <b> : \"" + longResult + "\"\n<c> <d> : F1\n<e> <f> <g> : \"done\"\n";
            seat.fComposeTable = xkb_compose_table_new_from_buffer(seat.fXkbContext, rules.data(), rules.size(),
                                                                   "C.UTF-8", XKB_COMPOSE_FORMAT_TEXT_V1,
                                                                   XKB_COMPOSE_COMPILE_NO_FLAGS);
            REQUIRE(seat.fComposeTable);
            seat.fComposeState = xkb_compose_state_new(seat.fComposeTable, XKB_COMPOSE_STATE_NO_FLAGS);
            REQUIRE(seat.fComposeState);
            press(KEY_A);
            press(KEY_B);
            press(KEY_C);
            press(KEY_D);
            REQUIRE(text == "a\xc3\xa9" + longResult);
            press(KEY_E);
            seat.dispatchText(KEY_E, true);
            press(KEY_F);
            seat.dispatchText(KEY_F, true);
            REQUIRE(text == "a\xc3\xa9" + longResult);
            press(KEY_G);
            REQUIRE(text == "a\xc3\xa9" + longResult + "done");
            REQUIRE(SetClipboardText(owner, std::string_view("a\0b", 3)) == ClipboardResult::UnknownError);
            REQUIRE(SetClipboardText(owner, std::string_view("\xc0\xaf", 2)) == ClipboardResult::UnknownError);
            unsigned repeated = 0;
            auto destroy = owner.Listen(
                [&](const AnyEvent& event)
                {
                    if (std::holds_alternative<EventTextInput>(event))
                    {
                        ++repeated;
                        REQUIRE(owner.Destroy() == Result::Success);
                    }
                    return EventResponse::Unhandled;
                });
            REQUIRE(destroy.has_value());
            seat.fPressedScanCodes.insert(KEY_A);
            seat.fRepeatingScanCode = KEY_A;
            seat.dispatchKeyRepeats(2);
            REQUIRE(repeated == 1);
            seat.windowRemoved(backend);
            seat.reset();
            REQUIRE(seat.inputSerial() == 0);
        }
    };
}  // namespace LWS::internal

TEST_CASE("Wayland is the compiled backend", "[platform][wayland]")
{
    const auto backends = LWS::PlatformContext::GetAvailableBackends();
    REQUIRE(std::ranges::find(backends, LWS::BackendId::Wayland) != backends.end());
    REQUIRE(std::ranges::find(backends, LWS::BackendId::X11) == backends.end());
}

TEST_CASE("Wayland context is explicit and one-shot", "[platform][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    REQUIRE(context.GetBackendId() == LWS::BackendId::Wayland);
    REQUIRE(context.Init({.backend = LWS::BackendId::Wayland}) == LWS::Result::InvalidState);
    REQUIRE(context.Shutdown() == LWS::Result::Success);
    REQUIRE(context.Init({.backend = LWS::BackendId::Wayland}) == LWS::Result::InvalidState);
}

TEST_CASE("Wayland monitor queries are scoped to the context", "[platform][monitor][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    const auto primary = context.GetPrimaryMonitor(true);
    REQUIRE(primary.has_value());
    if (primary->handle != 0)
        REQUIRE(context.GetMonitorInfo(primary->handle)->handle == primary->handle);
}

TEST_CASE("Wayland window exposes stable typed native objects", "[window][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    LWS::Window window(context);
    REQUIRE(LWS::Wayland::SetAppId(window, "io.github.openimageviewer") == LWS::Result::Success);
    REQUIRE(window.Create() == LWS::Result::Success);
    const auto surface = LWS::Wayland::GetSurface(window);
    const auto display = LWS::Wayland::GetDisplay(window);
    REQUIRE(surface.has_value());
    REQUIRE(display.has_value());
    REQUIRE(*surface != nullptr);
    REQUIRE(*display != nullptr);
    REQUIRE(window.SetVisible(true) == LWS::Result::Success);
    REQUIRE(LWS::Wayland::GetSurface(window) == surface);
    auto invalidDialog = [&](const LWS::ListFileDialogFilters& filters, const std::string& title,
                             const std::string& extension, const std::string& initial)
    {
        std::string output = "unchanged";
        REQUIRE(LWS::FileDialog::Show(LWS::FileDialogType::SaveFile, filters, title, window, extension, 0, initial,
                                      output) == LWS::FileDialogResult::UnknownError);
        REQUIRE(output == "unchanged");
        LWS::ListFileDialogFileNames outputs{"unchanged"};
        REQUIRE(LWS::FileDialog::Show(LWS::FileDialogType::SaveFile, filters, title, window, extension, 0, initial,
                                      outputs) == LWS::FileDialogResult::UnknownError);
        REQUIRE(outputs == LWS::ListFileDialogFileNames{"unchanged"});
    };
    invalidDialog({}, std::string(1, '\xff'), {}, "report");
    invalidDialog({{std::string(1, '\xff'), {"*.txt"}}}, "Save", {}, "report");
    invalidDialog({{"Text", {std::string("*.") + char(0xff)}}}, "Save", {}, "report");
    invalidDialog({}, "Save", std::string(1, '\xff'), "report");
    invalidDialog({}, "Save", {}, std::string("/tmp/") + char(0xff) + ".png");
    invalidDialog({}, "Save", {}, std::string("a\0b", 3));
    LWS::internal::WaylandTextInputTestAccess::Exercise(window);
}

TEST_CASE("Wayland top-level positions and multi-monitor fullscreen are unsupported", "[window][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    LWS::Window window(context);
    REQUIRE(window.Create() == LWS::Result::Success);
    REQUIRE(window.RequestPlacement({.position = LWS::Point{20, 30}}) == LWS::Result::NotSupported);
    REQUIRE_FALSE(window.GetPlacement().position.has_value());
    REQUIRE(window.SetWindowMode(LWS::WindowMode::FullscreenAllMonitors) == LWS::Result::NotSupported);
    REQUIRE(window.SetAlwaysOnTop(true) == LWS::Result::NotSupported);
    // Moving is supported, but requires a configured surface and a live pointer press serial.
    REQUIRE(window.BeginWindowDrag(LWS::WindowDragOperation::Move) == LWS::Result::InvalidState);
}

// Requires a compositor that honors fullscreen/maximize requests.
TEST_CASE("Wayland windowed maximization leaves fullscreen and retains normal size", "[.][window][maximize][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    LWS::Window window(context);
    REQUIRE(window.RequestShowState(LWS::WindowShowState::Maximized) == LWS::Result::InvalidState);
    LWS::WindowMode expectedMode = LWS::WindowMode::Windowed;
    LWS::WindowShowState expectedState = LWS::WindowShowState::Restored;
    bool confirmed = false;
    LWS::Result presentation = LWS::Result::Success;
    std::vector<std::byte> pixels;
    auto connection = window.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventPaint>(event) && window.IsConfigured())
            {
                // Commit each configured state, including intermediate ones, so the compositor can map the surface.
                const auto area = window.GetClientAreaMetrics();
                REQUIRE(area.pixels.has_value());
                pixels.resize(static_cast<size_t>(area.pixels->x) * area.pixels->y * 4, std::byte{0xff});
                presentation = window.PresentBitmap({
                    .pixels = pixels,
                    .format = LWS::BitmapPixelFormat::Bgra8Premultiplied,
                    .width = static_cast<uint32_t>(area.pixels->x),
                    .height = static_cast<uint32_t>(area.pixels->y),
                    .rowPitch = static_cast<uint32_t>(area.pixels->x) * 4U,
                });
                confirmed = window.GetWindowMode() == expectedMode && window.GetShowState() == expectedState;
            }
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(connection.has_value());
    const auto awaitConfigure = [&]
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!confirmed && std::chrono::steady_clock::now() < deadline)
        {
            std::ignore = context.ProcessMessages();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(presentation == LWS::Result::Success);
        REQUIRE(confirmed);
    };
    REQUIRE(window.Create({.clientSize = {320, 240}, .visible = true, .eraseBackground = false}) ==
            LWS::Result::Success);
    awaitConfigure();
    const auto original = window.GetClientAreaMetrics().logical;
    confirmed = false;
    expectedMode = LWS::WindowMode::Fullscreen;
    REQUIRE(window.SetWindowMode(expectedMode) == LWS::Result::Success);
    awaitConfigure();
    confirmed = false;
    expectedMode = LWS::WindowMode::Windowed;
    expectedState = LWS::WindowShowState::Maximized;
    REQUIRE(window.RequestShowState(LWS::WindowShowState::Maximized) == LWS::Result::Success);
    REQUIRE(window.RequestShowState(LWS::WindowShowState::Maximized) == LWS::Result::Success);
    awaitConfigure();
    confirmed = false;
    expectedState = LWS::WindowShowState::Restored;
    REQUIRE(window.RequestShowState(expectedState) == LWS::Result::Success);
    awaitConfigure();
    REQUIRE(window.GetClientAreaMetrics().logical == original);
    confirmed = false;
    expectedState = LWS::WindowShowState::Maximized;
    REQUIRE(window.SetWindowMode(LWS::WindowMode::Fullscreen) == LWS::Result::Success);
    REQUIRE(window.RequestShowState(LWS::WindowShowState::Maximized) == LWS::Result::Success);
    awaitConfigure();
    LWS::Window child(context);
    REQUIRE(child.Create({.parent = &window}) == LWS::Result::Success);
    REQUIRE(child.RequestShowState(LWS::WindowShowState::Maximized) == LWS::Result::NotSupported);
}

TEST_CASE("Wayland child containment uses parent configuration", "[window][parent][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    LWS::Window parent(context);
    LWS::Window child(context);
    REQUIRE(parent.Create({.visible = true}) == LWS::Result::Success);
    REQUIRE(child.Create({.parent = &parent, .clientSize = {320, 200}}) == LWS::Result::Success);
    REQUIRE(child.GetParent() == &parent);
    const auto surface = LWS::Wayland::GetSurface(child);
    REQUIRE(surface.has_value());
    REQUIRE(child.SetVisible(true) == LWS::Result::Success);
    REQUIRE(child.SetVisible(false) == LWS::Result::Success);
    REQUIRE(LWS::Wayland::GetSurface(child) == surface);
    REQUIRE(child.SetVisible(true) == LWS::Result::Success);
    REQUIRE(LWS::Wayland::GetSurface(child) == surface);
    REQUIRE(parent.Destroy() == LWS::Result::Success);
    REQUIRE_FALSE(child.IsCreated());
}

TEST_CASE("Wayland emits portable coherent metric events", "[window][event][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    LWS::Window window(context);
    bool observed{};
    auto connection = window.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (const auto* size = std::get_if<LWS::EventClientAreaSizeChanged>(&event))
                observed = size->size.logical.x > 0 && size->size.pixels.has_value() && size->size.pixels->x > 0;
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(connection.has_value());
    REQUIRE(window.Create({.visible = true}) == LWS::Result::Success);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!observed && std::chrono::steady_clock::now() < deadline)
    {
        std::ignore = context.ProcessMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(observed);
    REQUIRE(window.IsConfigured());
    REQUIRE(window.GetClientAreaMetrics().pixels.has_value());
}

TEST_CASE("Wayland custom cursors fail without changing standard selection", "[cursor][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    LWS::Window window(context);
    REQUIRE(window.SetMouseCursor(LWS::Cursor::FromShape(LWS::CursorShape::Hand)) == LWS::Result::Success);
    const std::array<std::byte, 4> pixel{};
    auto custom = LWS::Cursor::FromBitmap({.pixels = pixel, .width = 1, .height = 1, .rowPitch = 4}, {0, 0});
    REQUIRE(custom.has_value());
    REQUIRE(window.SetMouseCursor(*custom) == LWS::Result::NotSupported);
}

TEST_CASE("Wayland task wake executes FIFO", "[platform][task][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    std::vector<int> order;
    REQUIRE(context.PostTask([&] { order.push_back(1); }) == LWS::Result::Success);
    REQUIRE(context.PostTask([&] { order.push_back(2); }) == LWS::Result::Success);
    std::ignore = context.ProcessMessages();
    REQUIRE(order == std::vector{1, 2});
}

TEST_CASE("Wayland timer callbacks marshal through the context", "[timer][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    const auto ownerThread = std::this_thread::get_id();
    unsigned calls{};
    bool onOwnerThread = true;
    std::unique_ptr<LWS::HighPrecisionTimer> timer;
    bool destroyOnCallback = false;
    bool repeat = false;
    bool nested = false;
    SECTION("Immediate one-shot wakes a blocking loop") {}
    SECTION("Repeating timer can disable itself")
    {
        repeat = true;
    }
    SECTION("Callback can destroy its timer")
    {
        destroyOnCallback = true;
    }
    SECTION("Nested dispatch consumes readiness only once")
    {
        nested = true;
    }
    timer = std::make_unique<LWS::HighPrecisionTimer>(context,
                                                      [&]
                                                      {
                                                          ++calls;
                                                          onOwnerThread = onOwnerThread &&
                                                                          std::this_thread::get_id() == ownerThread;
                                                          if (nested)
                                                              std::ignore = context.ProcessMessages();
                                                          if (repeat && calls < 3)
                                                              return;
                                                          if (destroyOnCallback)
                                                              timer.reset();
                                                          else
                                                              timer->Enable(false);
                                                          context.RequestQuit();
                                                      });
    timer->SetDueTime(0);
    timer->SetRepeatInterval(repeat ? 1 : 0);
    timer->Enable(true);
    REQUIRE(context.RunMessageLoop() == LWS::LoopResult::Quit);
    REQUIRE(calls == (repeat ? 3 : 1));
    REQUIRE(onOwnerThread);
    if (timer)
        REQUIRE_FALSE(timer->GetEnabled());
}

TEST_CASE("Wayland configure tolerates destruction from paint callbacks", "[window][event][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    LWS::Window window(context);
    bool destroyed{};
    bool destroyFromMetrics{};
    SECTION("paint") {}
    SECTION("metrics")
    {
        destroyFromMetrics = true;
    }
    auto connection = window.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (destroyFromMetrics ? std::holds_alternative<LWS::EventClientAreaSizeChanged>(event)
                                   : std::holds_alternative<LWS::EventPaint>(event))
            {
                destroyed = window.Destroy() == LWS::Result::Success;
            }
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(connection.has_value());
    REQUIRE(window.Create({.visible = true, .eraseBackground = false}) == LWS::Result::Success);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!destroyed && std::chrono::steady_clock::now() < deadline)
    {
        std::ignore = context.ProcessMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(destroyed);
    REQUIRE_FALSE(window.IsCreated());
}

TEST_CASE("Wayland timer restart discards an already queued expiration", "[timer][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    unsigned callbacks{};
    auto timer = std::make_unique<LWS::HighPrecisionTimer>(context, [&] { ++callbacks; });
    timer->SetDueTime(1);
    timer->SetRepeatInterval(0);
    timer->Enable(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    SECTION("Restart before dispatch")
    {
        timer->Enable(false);
        timer->SetDueTime(60'000);
        timer->Enable(true);
    }
    SECTION("Restart while dispatching a ready descriptor snapshot")
    {
        REQUIRE(context.PostTask([&] { timer->SetDueTime(60'000); }) == LWS::Result::Success);
    }
    SECTION("Destroy and replace while dispatching a ready descriptor snapshot")
    {
        REQUIRE(context.PostTask(
                    [&]
                    {
                        timer.reset();
                        timer = std::make_unique<LWS::HighPrecisionTimer>(context, [&] { ++callbacks; });
                        timer->SetDueTime(60'000);
                        timer->Enable(true);
                    }) == LWS::Result::Success);
    }
    std::ignore = context.ProcessMessages();
    REQUIRE(callbacks == 0);
}

TEST_CASE("Wayland timer null target detaches and retains its interval", "[timer][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    LWS::Window window(context);
    REQUIRE(window.Create() == LWS::Result::Success);
    unsigned callbacks{};
    LWS::Timer timer(context);
    timer.SetCallback([&] { ++callbacks; });
    REQUIRE(timer.SetTargetWindow(&window) == LWS::Result::Success);
    timer.SetInterval(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    REQUIRE(timer.SetTargetWindow(nullptr) == LWS::Result::Success);
    REQUIRE(timer.GetInterval() == 1);
    std::ignore = context.ProcessMessages();
    REQUIRE(callbacks == 0);
    REQUIRE(timer.SetTargetWindow(&window) == LWS::Result::Success);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (callbacks == 0 && std::chrono::steady_clock::now() < deadline)
    {
        std::ignore = context.ProcessMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(callbacks > 0);
}

#endif
