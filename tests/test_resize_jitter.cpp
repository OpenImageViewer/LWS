#ifdef LWS_TEST_UI
    #include <catch2/catch_test_macros.hpp>
    #include <catch2/catch_approx.hpp>
    #include <catch2/generators/catch_generators.hpp>
    #include <LWSUI/Containers.hpp>
    #include <LWSUI/UIHost.hpp>
    #include <algorithm>
    #include <array>
    #include <cmath>
    #include <functional>
    #include <limits>
    #include <vector>
    #ifdef LWS_HAS_WIN32_BACKEND
        #include <LWS/Win32/Platform.hpp>
        #include <LWS/Win32/WindowExtensions.hpp>
    #endif
namespace
{
    class IntrinsicItem final : public LWSUI::Control
    {
      protected:

        LWSUI::Size OnMeasure(LWSUI::Size) override { return {100, 40}; }
    };
}  // namespace
TEST_CASE("Flow rows follow actual allocation and remain stable on repeated layout", "[ui][resize-jitter][flow]")
{
    LWSUI::FlowPanel flow;
    flow.gap = flow.rowGap = 10;
    auto& first = flow.AddItem(std::make_unique<IntrinsicItem>());
    auto& second = flow.AddItem(std::make_unique<IntrinsicItem>());
    REQUIRE(flow.Measure({210.4f, 100}).height == 40);
    flow.Arrange({0, 0, 209.6f, 100});
    REQUIRE(second.Bounds().y > first.Bounds().y);
    REQUIRE(flow.DesiredSize().height == 90);
    const auto wrappedY = second.Bounds().y;
    for (int pass = 0; pass < 6; ++pass)
    {
        REQUIRE(flow.Measure({210.4f, 100}).height == 90);
        flow.Arrange({0, 0, 209.6f, 100});
        REQUIRE(second.Bounds().y == wrappedY);
        REQUIRE(second.Bounds().x + second.Bounds().width <= flow.Bounds().width);
    }
    flow.Measure({212, 100});
    flow.Arrange({0, 0, 212, 100});
    REQUIRE(second.Bounds().y == first.Bounds().y);
    flow.Measure({210.4f, 100});
    flow.Arrange({0, 0, 209.6f, 100});
    REQUIRE(second.Bounds().y == wrappedY);
}
TEST_CASE("Flow resize sweeps produce one wrap transition per direction", "[ui][resize-jitter][flow]")
{
    LWSUI::FlowPanel flow;
    flow.gap = flow.rowGap = 10;
    auto& first = flow.AddItem(std::make_unique<IntrinsicItem>());
    auto& second = flow.AddItem(std::make_unique<IntrinsicItem>());
    int transitions = 0;
    bool previous = true;
    for (int width = 205; width <= 215; ++width)
    {
        flow.Measure({float(width) + .4f, 100});
        flow.Arrange({0, 0, float(width) - .4f, 100});
        const bool wrapped = second.Bounds().y > first.Bounds().y;
        if (wrapped != previous)
            ++transitions;
        previous = wrapped;
    }
    REQUIRE(transitions == 1);
    transitions = 0;
    for (int width = 215; width >= 205; --width)
    {
        flow.Measure({float(width) + .4f, 100});
        flow.Arrange({0, 0, float(width) - .4f, 100});
        const bool wrapped = second.Bounds().y > first.Bounds().y;
        if (wrapped != previous)
            ++transitions;
        previous = wrapped;
    }
    REQUIRE(transitions == 1);
}

namespace
{
    class WidthSensitiveItem final : public LWSUI::Control
    {
      protected:

        LWSUI::Size OnMeasure(LWSUI::Size space) override { return {space.width, space.width >= 210 ? 40.f : 90.f}; }
        LWSUI::Size OnMeasureContent(LWSUI::Size) override { return {210.4f, 40}; }
    };
}  // namespace
TEST_CASE("General grid layout measures wrapped children at the allocated width", "[ui][resize-jitter][layout]")
{
    LWSUI::Grid grid;
    grid.columns = {-1, -1};
    grid.spacing = 10;
    auto& first = grid.Emplace<WidthSensitiveItem>();
    grid.Emplace<WidthSensitiveItem>();
    REQUIRE(grid.Measure({430.4f, 200}).height == 40);
    grid.Arrange({0, 0, 429.6f, 200});
    REQUIRE(grid.DesiredSize().height == 90);
    REQUIRE(first.Bounds().height == 90);
    for (int pass = 0; pass < 6; ++pass)
    {
        REQUIRE(grid.Measure({430.4f, 200}).height == 90);
        grid.Arrange({0, 0, 429.6f, 200});
        REQUIRE(first.Bounds().height == 90);
    }
}
TEST_CASE("Allocation hints survive alternating intrinsic and minimum-width probes", "[ui][resize-jitter][layout]")
{
    WidthSensitiveItem item;
    item.MeasureContent({500, 200});
    item.Measure({210.4f, 200});
    item.Arrange({0, 0, 209.6f, 200});
    REQUIRE(item.DesiredSize().height == 90);
    for (int pass = 0; pass < 6; ++pass)
    {
        REQUIRE(item.MeasureContent({500, 200}).width == 210.4f);
        item.Measure({0, 200});
        REQUIRE(item.Measure({210.4f, 200}).height == 90);
        item.Arrange({0, 0, 209.6f, 200});
        REQUIRE(item.DesiredSize().height == 90);
    }
}
TEST_CASE("Explicit layout changes discard detached allocation corrections", "[ui][resize-jitter][layout]")
{
    WidthSensitiveItem item;
    item.Measure({210.4f, 200});
    item.Arrange({0, 0, 209.6f, 200});
    REQUIRE(item.Measure({210.4f, 200}).height == 90);
    item.Invalidate(true);
    REQUIRE(item.Measure({210.4f, 200}).height == 40);

    LWSUI::FlowPanel flow;
    flow.gap = flow.rowGap = 10;
    auto& child = flow.AddItem(std::make_unique<IntrinsicItem>());
    flow.AddItem(std::make_unique<IntrinsicItem>());
    flow.Measure({210.4f, 100});
    flow.Arrange({0, 0, 209.6f, 100});
    REQUIRE(flow.DesiredSize().height == 90);
    child.Invalidate(true);
    REQUIRE(flow.Measure({210.4f, 100}).height == 40);
}

#endif
