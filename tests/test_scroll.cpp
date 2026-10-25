#ifdef LWS_TEST_UI
    #include <catch2/catch_test_macros.hpp>
    #include <catch2/catch_approx.hpp>
    #include <LWSUI/Composites.hpp>
    #include <LWSUI/UIHost.hpp>
    #include "demo/ScrollFeedback.hpp"
    #include <LWS/Platform.hpp>
    #include <LWS/TextClipboard.hpp>
    #ifdef LWS_HAS_WIN32_BACKEND
        #include <LWS/Win32/Platform.hpp>
    #endif
    #include <limits>
    #include <cmath>
    #include <thread>
namespace LWSUI::internal
{
    struct UIHostTestAccess
    {
        static bool Event(UIHost& host, const LWS::AnyEvent& event) { return host.Event(event); }
        static Control* Captured(UIHost& host) { return host.capture_.Get(); }
    };
}
using namespace LWSUI;
namespace
{
    struct Environment
    {
        LWS::PlatformContext context;
        std::unique_ptr<LWS::Window> window;
        std::unique_ptr<UIHost> host;
        Environment()
        {
    #ifdef LWS_HAS_WIN32_BACKEND
            REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
            REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    #else
            REQUIRE(context.Init({.backend = LWS::BackendId::Wayland}) == LWS::Result::Success);
    #endif
            window = std::make_unique<LWS::Window>(context);
            REQUIRE(window->Create({.clientSize = {640, 480}, .visible = true}) == LWS::Result::Success);
            host = std::make_unique<UIHost>(*window);
    #ifndef LWS_HAS_WIN32_BACKEND
            for (int i = 0; i < 500 && !window->IsConfigured(); ++i)
            {
                REQUIRE(context.ProcessMessages() == LWS::LoopResult::Continue);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            REQUIRE(window->IsConfigured());
    #endif
        }
    };
}
TEST_CASE("Horizontal scrollbar preserves vertical default and uses horizontal input", "[ui][grid][scrollbar]")
{
    ScrollBar bar;
    REQUIRE(bar.GetOrientation() == Orientation::Vertical);
    bar.SetOrientation(Orientation::Horizontal);
    bar.SetPage(.5);
    bar.Arrange({0, 0, 200, 20});
    bar.Dispatch({.kind = InputKind::Down, .x = 150, .y = 10});
    bar.Dispatch({.kind = InputKind::Up, .x = 150, .y = 10});
    REQUIRE(bar.Value() > .9);
    bar.Dispatch({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Left});
    REQUIRE(bar.Value() < 1);
}
TEST_CASE("Outward acceleration uses bounded multipliers and continuous relative dragging", "[ui][grid][scrollbar]")
{
    for (auto orientation : {Orientation::Vertical, Orientation::Horizontal})
        for (bool mirrored : {false, true})
            for (bool track : {false, true})
            {
                ScrollBar bar;
                const bool vertical = orientation == Orientation::Vertical;
                bar.SetOrientation(orientation);
                const float origin = mirrored ? 0 : 380;
                bar.Arrange(vertical ? Rect{origin, 0, 20, 400} : Rect{0, origin, 400, 20});
                bar.SetPage(.1);
                bar.SetValue(.5);
                bar.ConfigureResolution({0, 0, 400, 400}, 1000000, 28);
                bar.SetDynamicResolution(true);
                REQUIRE_FALSE(bar.OutwardAcceleration());
                const float cross = origin + 10, axis = track ? 300 : 200;
                const float outer = mirrored ? 0 : 400, sign = mirrored ? -1 : 1;
                const auto input = [&](InputKind kind, float perpendicular, float along)
                {
                    return bar.Dispatch({.kind = kind, .x = vertical ? perpendicular : along,
                                          .y = vertical ? along : perpendicular});
                };
                input(InputKind::Down, cross, axis);
                input(InputKind::Move, outer + sign * 40, axis);
                REQUIRE(bar.ScrollSpeed() == 100);
                input(InputKind::Up, outer + sign * 40, axis);
                bar.SetOutwardAcceleration(true);
                bar.SetValue(.5);
                input(InputKind::Down, cross, axis);
                const auto initial = bar.Value();
                input(InputKind::Move, outer, axis);
                REQUIRE(bar.ScrollSpeed() == 100);
                input(InputKind::Move, outer + sign * 20, axis);
                REQUIRE(bar.ScrollSpeed() == Catch::Approx(200));
                REQUIRE(bar.Value() == initial);
                input(InputKind::Move, outer + sign * 20, axis + 1);
                REQUIRE(bar.Value() - initial == Catch::Approx(2. / 360));
                const auto intermediate = bar.Value();
                input(InputKind::Move, outer + sign * 400, axis + 1);
                REQUIRE(bar.ScrollSpeed() == Catch::Approx(400));
                REQUIRE(bar.Value() == intermediate);
                input(InputKind::Move, outer + sign * 40, axis + 2);
                REQUIRE(bar.Value() - intermediate == Catch::Approx(4. / 360));
                const auto boosted = bar.Value();
                input(InputKind::Move, cross, axis + 2);
                REQUIRE(bar.ScrollSpeed() == 100);
                REQUIRE(bar.Value() == boosted);
                input(InputKind::Move, 200, axis + 2);
                REQUIRE(bar.ScrollSpeed() < 100);
                REQUIRE(bar.Value() == boosted);
                input(InputKind::Move, outer + sign * 40, 100000);
                REQUIRE(bar.Value() == 1);
                input(InputKind::Move, outer + sign * 40, 99999);
                REQUIRE(bar.Value() == Catch::Approx(1 - 4. / 360));
                input(InputKind::Move, outer + sign * 40, -100000);
                REQUIRE(bar.Value() == 0);
                input(InputKind::Cancel, cross, axis);
                REQUIRE(bar.Value() == Catch::Approx(.5));
                REQUIRE_FALSE(bar.ResolutionDragging());
                REQUIRE(bar.ScrollSpeed() == 100);
                bar.SetMaximumScrollSpeed(100);
                input(InputKind::Down, cross, 200);
                input(InputKind::Move, outer + sign * 40, 200);
                REQUIRE(bar.ScrollSpeed() == 100);
                bar.SetMaximumScrollSpeed(900);
                REQUIRE_FALSE(bar.ResolutionDragging());
                input(InputKind::Down, cross, 200);
                input(InputKind::Move, outer + sign * 20, 200);
                REQUIRE(bar.ScrollSpeed() == Catch::Approx(300));
                bar.SetOutwardDistance(.2);
                REQUIRE_FALSE(bar.ResolutionDragging());
                input(InputKind::Down, cross, 200);
                input(InputKind::Move, outer + sign * 40, 200);
                REQUIRE(bar.ScrollSpeed() == Catch::Approx(300));
                bar.SetVisible(false);
                REQUIRE_FALSE(bar.ResolutionDragging());
                REQUIRE(bar.ScrollSpeed() == 100);
                bar.SetVisible(true);
                input(InputKind::Down, cross, 200);
                input(InputKind::Move, outer + sign * 80, 200);
                input(InputKind::Up, outer + sign * 80, 201);
                REQUIRE_FALSE(bar.ResolutionDragging());
                REQUIRE(bar.ScrollSpeed() == 100);
                input(InputKind::Down, cross, 200);
                bar.SetOutwardAcceleration(false);
                REQUIRE_FALSE(bar.ResolutionDragging());
                REQUIRE(bar.ScrollSpeed() == 100);
            }
    ScrollBar bar;
    for (double invalid : {0., -1., 1.1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        REQUIRE_THROWS(bar.SetOutwardDistance(invalid));
    for (double invalid : {99., -1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        REQUIRE_THROWS(bar.SetMaximumScrollSpeed(invalid));
}
TEST_CASE("Dynamic resolution uses relative motion and exponential sensitivity", "[ui][grid][scrollbar]")
{
    for (auto orientation : {Orientation::Vertical, Orientation::Horizontal})
    {
        ScrollBar bar;
        bar.SetResolutionFraction(1);
        bar.SetInwardDistance(.45);
        REQUIRE_FALSE(bar.DynamicResolution());
        bar.SetOrientation(orientation);
        const bool vertical = orientation == Orientation::Vertical;
        bar.Arrange(vertical ? Rect{380, 0, 20, 400} : Rect{0, 380, 400, 20});
        bar.SetPage(.1);
        bar.SetValue(.5);
        bar.ConfigureResolution({0, 0, 400, 400}, 1000000000., 28);
        bar.SetDynamicResolution(true);
        const auto input = [&](InputKind kind, float cross, float axis)
        {
            return bar.Dispatch({.kind = kind, .x = vertical ? cross : axis, .y = vertical ? axis : cross});
        };
        input(InputKind::Down, 390, 200);
        REQUIRE(bar.ResolutionDragging());
        const auto initial = bar.Value();
        input(InputKind::Move, 200, 200);
        REQUIRE(bar.Value() == initial);
        REQUIRE(bar.ScrollSpeed() == Catch::Approx(1));
        input(InputKind::Move, 200, 201);
        REQUIRE((bar.Value() - initial) * 1000000000. == Catch::Approx(28).margin(.0001));
        const auto fine = bar.Value();
        input(InputKind::Move, 290, 201);
        REQUIRE(bar.Value() == fine);
        REQUIRE(bar.ScrollSpeed() == Catch::Approx(10));
        input(InputKind::Move, 290, 202);
        const auto normal = 1000000000. / 360;
        REQUIRE((bar.Value() - fine) * 1000000000. == Catch::Approx(std::sqrt(normal * 28)).margin(.001));
        input(InputKind::Move, 100, 202);
        REQUIRE(bar.ScrollSpeed() == Catch::Approx(1));
        input(InputKind::Cancel, 100, 202);
        REQUIRE(bar.Value() == Catch::Approx(.5));
        REQUIRE_FALSE(bar.ResolutionDragging());
        REQUIRE(bar.ScrollSpeed() == 100);
        bar.ConfigureResolution({0, 0, 400, 400}, 1000000000., 28, ScrollResolution::Pixel);
        input(InputKind::Down, 390, 200);
        input(InputKind::Move, 200, 200);
        const auto pixel = bar.Value();
        input(InputKind::Move, 200, 201);
        REQUIRE((bar.Value() - pixel) * 1000000000. == Catch::Approx(1).margin(.0001));
        REQUIRE(bar.ScrollSpeed() == Catch::Approx(1. / 28));
        input(InputKind::Up, 200, 201);
        REQUIRE(bar.ScrollSpeed() == 100);
        REQUIRE_FALSE(bar.ResolutionDragging());
    }
}
TEST_CASE("Dynamic resolution handles opposite edges limits and lifecycle", "[ui][grid][scrollbar]")
{
    for (auto orientation : {Orientation::Vertical, Orientation::Horizontal})
    {
        ScrollBar bar;
        bar.SetResolutionFraction(1);
        bar.SetInwardDistance(.45);
        bar.SetOrientation(orientation);
        const bool vertical = orientation == Orientation::Vertical;
        bar.Arrange(vertical ? Rect{0, 0, 20, 400} : Rect{0, 0, 400, 20});
        bar.SetPage(.1);
        bar.ConfigureResolution({0, 0, 400, 400}, 1000000, 28);
        bar.SetDynamicResolution(true);
        const auto input = [&](InputKind kind, float cross, float axis)
        {
            return bar.Dispatch({.kind = kind, .x = vertical ? cross : axis, .y = vertical ? axis : cross});
        };
        const auto begin = [&]
        {
            bar.SetValue(.5);
            input(InputKind::Down, 10, 200);
            input(InputKind::Move, 200, 200);
            REQUIRE(bar.ScrollSpeed() == Catch::Approx(1));
        };
        begin();
        input(InputKind::Move, 200, 100000);
        REQUIRE(bar.Value() == 1);
        input(InputKind::Move, 200, 99999);
        REQUIRE(bar.Value() == Catch::Approx(1 - 28. / 1000000));
        input(InputKind::Move, 200, -100000);
        REQUIRE(bar.Value() == 0);
        input(InputKind::Up, 200, -100000);
        begin();
        input(InputKind::Move, -100, 200);
        REQUIRE(bar.ScrollSpeed() == 100);
        REQUIRE(bar.Value() == .5);
        bar.SetDynamicResolution(false);
        REQUIRE_FALSE(bar.ResolutionDragging());
        bar.SetDynamicResolution(true);
        begin();
        bar.SetOrientation(vertical ? Orientation::Horizontal : Orientation::Vertical);
        REQUIRE_FALSE(bar.ResolutionDragging());
        REQUIRE(bar.ScrollSpeed() == 100);
        bar.SetOrientation(orientation);
        begin();
        bar.ConfigureResolution({0, 0, 400, 400}, 1000000, 30);
        REQUIRE_FALSE(bar.ResolutionDragging());
        REQUIRE(bar.ScrollSpeed() == 100);
        bar.ConfigureResolution({0, 0, 400, 400}, 1000000, 28);
        begin();
        bar.SetPage(.2);
        REQUIRE_FALSE(bar.ResolutionDragging());
        REQUIRE(bar.ScrollSpeed() == 100);
        bar.SetPage(.1);
        begin();
        bar.Arrange(vertical ? Rect{0, 0, 20, 500} : Rect{0, 0, 500, 20});
        REQUIRE_FALSE(bar.ResolutionDragging());
        REQUIRE(bar.ScrollSpeed() == 100);
        bar.Arrange(vertical ? Rect{0, 0, 20, 400} : Rect{0, 0, 400, 20});
        begin();
        bar.SetVisible(false);
        REQUIRE_FALSE(bar.ResolutionDragging());
        REQUIRE(bar.ScrollSpeed() == 100);
    }
    ScrollBar bar;
    bar.SetDynamicResolution(true);
    bar.Arrange({380, 0, 20, 400});
    bar.ConfigureResolution({0, 0, 400, 400}, 0, 28);
    bar.Dispatch({.kind = InputKind::Down, .x = 390, .y = 200});
    REQUIRE_FALSE(bar.ResolutionDragging());
    bar.Finish(EditPhase::Cancel);
    REQUIRE_THROWS(bar.SetPage(std::numeric_limits<double>::quiet_NaN()));
    REQUIRE_THROWS(bar.ConfigureResolution({0, 0, 400, 400}, -1, 28));
    REQUIRE_THROWS(bar.ConfigureResolution({0, 0, 400, 400}, 100, 0));
}
TEST_CASE("Track presses engage dynamic resolution and release applies final motion", "[ui][grid][scrollbar]")
{
    for (auto orientation : {Orientation::Vertical, Orientation::Horizontal})
    {
        ScrollBar bar;
        bar.SetResolutionFraction(1);
        bar.SetInwardDistance(.45);
        const bool vertical = orientation == Orientation::Vertical;
        bar.SetOrientation(orientation);
        bar.Arrange(vertical ? Rect{380, 0, 20, 400} : Rect{0, 380, 400, 20});
        bar.SetPage(.1);
        bar.ConfigureResolution({0, 0, 400, 400}, 1000000, 28);
        bar.SetDynamicResolution(true);
        const auto input = [&](InputKind kind, float cross, float axis, LWS::MouseButton button = LWS::MouseButton::Left)
        {
            return bar.Dispatch({.kind = kind, .x = vertical ? cross : axis, .y = vertical ? axis : cross, .button = button});
        };
        for (float cross : {380.f, 390.f, 399.f})
        {
            bar.SetValue(.1);
            input(InputKind::Down, cross, 300);
            REQUIRE(bar.Value() > .7);
            REQUIRE(bar.ResolutionDragging());
            input(InputKind::Move, 200, 300);
            const double jumped = bar.Value();
            REQUIRE(bar.ScrollSpeed() == Catch::Approx(1));
            input(InputKind::Up, 200, 301, LWS::MouseButton::Right);
            REQUIRE(bar.ResolutionDragging());
            REQUIRE(bar.Value() == jumped);
            input(InputKind::Down, 200, 301);
            REQUIRE(bar.Value() == jumped);
            input(InputKind::Up, 200, 301);
            REQUIRE(bar.Value() == Catch::Approx(jumped + 28. / 1000000));
            REQUIRE_FALSE(bar.ResolutionDragging());
            REQUIRE(bar.ScrollSpeed() == 100);
            bar.SetValue(.1);
            input(InputKind::Down, cross, 300);
            input(InputKind::Cancel, 200, 300);
            REQUIRE(bar.Value() == Catch::Approx(.1));
        }
        bar.SetValue(.1);
        auto disable = bar.OnScrollSpeed.Connect([&](double speed)
        {
            if (speed < 100) bar.SetDynamicResolution(false);
        });
        input(InputKind::Down, 390, 300);
        input(InputKind::Move, 200, 301);
        REQUIRE_FALSE(bar.ResolutionDragging());
        REQUIRE(bar.Value() == Catch::Approx(.1));
        REQUIRE(bar.ScrollSpeed() == 100);
    }
}
TEST_CASE("Configured inward percentages control both scrollbar axes", "[ui][grid][scrollbar]")
{
    for (auto orientation : {Orientation::Vertical, Orientation::Horizontal})
        for (bool opposite : {false, true})
            for (double distance : {.3, .5, 1.})
            {
                ScrollBar bar;
                bar.SetResolutionFraction(1);
                bar.SetOrientation(orientation);
                const bool vertical = orientation == Orientation::Vertical;
                bar.Arrange(vertical ? Rect{opposite ? 0.f : 380.f, 0, 20, 400}
                                     : Rect{0, opposite ? 0.f : 380.f, 400, 20});
                bar.SetPage(.1);
                bar.SetValue(.5);
                bar.ConfigureResolution({0, 0, 400, 400}, 1000000, 28);
                bar.SetInwardDistance(distance);
                bar.SetDynamicResolution(true);
                const auto input = [&](InputKind kind, float cross, float axis)
                {
                    return bar.Dispatch({.kind = kind, .x = vertical ? cross : axis, .y = vertical ? axis : cross});
                };
                input(InputKind::Down, opposite ? 10 : 390, 200);
                const float edge = opposite ? 20 : 380, direction = opposite ? 1 : -1;
                input(InputKind::Move, edge + direction * float(400 * distance / 2), 200);
                REQUIRE(bar.ScrollSpeed() == Catch::Approx(10));
                input(InputKind::Move, edge + direction * float(400 * distance), 200);
                REQUIRE(bar.ScrollSpeed() == Catch::Approx(1));
                const auto value = bar.Value();
                input(InputKind::Move, edge + direction * float(400 * distance), 201);
                REQUIRE(bar.Value() == Catch::Approx(value + 28. / 1000000));
                bar.SetInwardDistance(distance == .3 ? .5 : .3);
                REQUIRE_FALSE(bar.ResolutionDragging());
                REQUIRE(bar.Value() == .5);
            }
    ScrollBar bar;
    REQUIRE_THROWS(bar.SetInwardDistance(0));
    REQUIRE_THROWS(bar.SetInwardDistance(1.01));
    REQUIRE_THROWS(bar.SetInwardDistance(std::numeric_limits<double>::quiet_NaN()));
    bar.Arrange({380, 0, 20, 400});
    bar.SetPage(.1);
    bar.ConfigureResolution({0, 0, 400, 400}, 100, 28);
    bar.SetDynamicResolution(true);
    bar.Dispatch({.kind = InputKind::Down, .x = 390, .y = 20});
    bar.Dispatch({.kind = InputKind::Move, .x = 180, .y = 20});
    REQUIRE(bar.ScrollSpeed() == 100);
    bar.Finish(EditPhase::Commit);
}
TEST_CASE("Two-axis ScrollView preserves child input and dynamic captured drags", "[ui][scrollview]")
{
    Environment env;
    auto widget = std::make_unique<ScrollView>();
    auto* scroll = widget.get();
    auto cards = std::make_unique<Grid>();
    cards->columns = {200, 200, 200};
    auto& box = cards->Emplace<CheckBox>("Keep me");
    auto& text = cards->Emplace<TextBox>("Original");
    cards->Emplace<Button>("Button");
    scroll->SetContent(std::move(cards));
    ScrollViewOptions options;
    options.horizontalScroll = options.dynamicResolution = true;
    options.resolution = ScrollResolution::Pixel;
    options.resolutionFraction = 1;
    scroll->SetOptions(options);
    scroll->SetContentMinimumSize({3000, 3000});
    env.host->SetRoot(std::move(widget));
    env.host->Update();
    auto* vertical = static_cast<ScrollBar*>(scroll->Children()[0].get());
    auto* horizontal = static_cast<ScrollBar*>(scroll->Children()[1].get());
    REQUIRE(vertical->Visible());
    REQUIRE(horizontal->Visible());
    const auto click = [&](Control& target)
    {
        const auto b = target.Bounds();
        REQUIRE(env.host->Route({.kind = InputKind::Down, .x = b.x + 5, .y = b.y + b.height / 2}));
        REQUIRE(env.host->Route({.kind = InputKind::Up, .x = b.x + 5, .y = b.y + b.height / 2}));
    };
    click(box);
    REQUIRE(box.Value());
    click(text);
    REQUIRE(env.host->Route({.kind = InputKind::KeyDown, .key = LWS::KeyCode::End}));
    REQUIRE(env.host->Route({.kind = InputKind::Text, .text = " edited"}));
    REQUIRE(env.host->Focus(&box));
    scroll->SetScrollOffset(1000, 1000);
    scroll->SetScrollOffset(0, 0);
    REQUIRE(box.Value());
    REQUIRE(text.Text() == "Original edited");
    for (auto* bar : {vertical, horizontal})
    {
        const auto b = bar->Bounds();
        const bool v = bar == vertical;
        const float x = b.x + b.width / 2, y = b.y + b.height / 2;
        REQUIRE(env.host->Route({.kind = InputKind::Down, .x = x, .y = y}));
        REQUIRE(bar->ResolutionDragging());
        const auto before = scroll->ScrollOffset();
        REQUIRE(env.host->Route({.kind = InputKind::Move, .x = v ? -100 : x, .y = v ? y : -100}));
        REQUIRE(env.host->Route({.kind = InputKind::Up, .x = v ? -100 : x + 1, .y = v ? y + 1 : -100}));
        const auto after = scroll->ScrollOffset();
        REQUIRE((v ? after.height - before.height : after.width - before.width) == Catch::Approx(1).margin(.001));
        REQUIRE_FALSE(bar->ResolutionDragging());
    }
    scroll->SetScrollOffset(0, 0);
    const auto b = scroll->Bounds();
    REQUIRE(env.host->Route({.kind = InputKind::Wheel, .x = b.x + 10, .y = b.y + 10, .wheel = -1, .shift = true}));
    REQUIRE(scroll->ScrollOffset().width > 0);
    REQUIRE(scroll->Offset() == 0);
    REQUIRE(env.host->Route({.kind = InputKind::Wheel, .x = b.x + 10, .y = b.y + 10, .wheel = -1}));
    REQUIRE(scroll->Offset() > 0);
    for (bool overlay : {false, true})
    {
        scroll->overlayScrollBar = overlay;
        scroll->Measure({640, 480});
        scroll->Arrange({0, 0, 640, 480});
        REQUIRE(scroll->HitTest(horizontal->Bounds().x + 5, horizontal->Bounds().y + 5) == horizontal);
        REQUIRE(scroll->HitTest(vertical->Bounds().x + 5, vertical->Bounds().y + 5) == vertical);
    }
    scroll->overlayScrollBar = false;
    scroll->SetScrollOffset(100000, 100000);
    const auto maximum = scroll->ScrollOffset();
    REQUIRE(maximum.width < 3000);
    REQUIRE(maximum.height < 3000);
    scroll->Measure({1200, 900});
    scroll->Arrange({0, 0, 1200, 900});
    REQUIRE(scroll->ScrollOffset().width < maximum.width);
    REQUIRE(scroll->ScrollOffset().height < maximum.height);
    options.horizontalScroll = false;
    scroll->SetOptions(options);
    env.host->Update();
    REQUIRE(scroll->ScrollOffset().width == 0);
    REQUIRE_FALSE(horizontal->Visible());
    scroll->SetContent({});
    env.host->Update();
    REQUIRE(scroll->ScrollOffset().width == 0);
    REQUIRE(scroll->Offset() == 0);
    REQUIRE_FALSE(vertical->Visible());
    REQUIRE_FALSE(horizontal->Visible());
    options.inwardDistance = 0;
    REQUIRE_THROWS(scroll->SetOptions(options));
    REQUIRE_THROWS(scroll->SetContentMinimumSize({-1, 0}));
}
TEST_CASE("ScrollView reuses intrinsic measurement and invalidates on geometry changes", "[ui][scrollview][cache]")
{
    class MeasuredContent final : public Container
    {
      public:
        int measures = 0;
        Size extent{3000, 3000};
      protected:
        Size OnMeasure(Size) override { ++measures; return extent; }
    };
    ScrollView scroll;
    auto content = std::make_unique<MeasuredContent>();
    auto* measured = content.get();
    auto& child = measured->Emplace<Label>("Initial");
    scroll.SetOptions({.horizontalScroll = true});
    scroll.SetContent(std::move(content));
    scroll.Measure({640, 480});
    scroll.Arrange({0, 0, 640, 480});
    scroll.SetScrollOffset(1, 1);
    const auto initial = measured->measures;
    for (int i = 2; i < 100; ++i) scroll.SetScrollOffset(float(i), float(i));
    REQUIRE(measured->measures == initial);
    child.SetText("Longer text that changes intrinsic width");
    scroll.SetScrollOffset(100, 100);
    REQUIRE(measured->measures > initial);
    auto previous = measured->measures;
    measured->Emplace<Button>("New child");
    scroll.SetScrollOffset(101, 101);
    REQUIRE(measured->measures > previous);
    previous = measured->measures;
    auto theme = scroll.Style();
    theme.font.size += 2;
    scroll.SetStyle(theme);
    scroll.SetScrollOffset(102, 102);
    REQUIRE(measured->measures > previous);
    previous = measured->measures;
    scroll.overlayScrollBar = true;
    scroll.SetScrollOffset(103, 103);
    REQUIRE(measured->measures > previous);
    previous = measured->measures;
    scroll.Arrange({0, 0, 800, 600});
    REQUIRE(measured->measures > previous);
    measured->extent = {100, 100};
    measured->Invalidate(true);
    scroll.SetScrollOffset(104, 104);
    REQUIRE(scroll.ScrollOffset().width == 0);
    REQUIRE(scroll.ScrollOffset().height == 0);
}
TEST_CASE("Fixed speed feedback does not trigger content remeasurement", "[ui][scrollview][cache]")
{
    class MeasuredContent final : public Control
    {
      public:
        int measures = 0;
      protected:
        Size OnMeasure(Size) override { ++measures; return {3000, 3000}; }
    };
    Environment env;
    auto root = std::make_unique<DockPanel>();
    auto feedback = std::make_unique<demo::SpeedReadout>();
    auto* readout = feedback.get();
    root->AddDocked(std::move(feedback), Dock::Top);
    auto scroll = std::make_unique<ScrollView>();
    auto* view = scroll.get();
    auto content = std::make_unique<MeasuredContent>();
    auto* measured = content.get();
    view->SetOptions({.horizontalScroll = true});
    view->SetContent(std::move(content));
    root->AddDocked(std::move(scroll), Dock::Fill);
    env.host->SetRoot(std::move(root));
    env.host->Update();
    view->SetScrollOffset(1, 1);
    env.host->Update();
    const auto initial = measured->measures;
    for (int i = 2; i < 20; ++i)
    {
        readout->SetSpeed(100. / i);
        view->SetScrollOffset(float(i), float(i));
        env.host->Update();
    }
    REQUIRE(measured->measures == initial);
    auto theme = env.host->Style();
    theme.font.size += 3;
    env.host->SetTheme(theme);
    env.host->Update();
    REQUIRE(measured->measures > initial);
}
#endif
