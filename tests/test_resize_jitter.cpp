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

    #ifdef LWS_HAS_WIN32_BACKEND
namespace
{
    class LayoutChangeProbe final : public LWSUI::Control
    {
      public:

        std::function<void()> arranged;
        int paints = 0;

      protected:

        LWSUI::Size OnMeasure(LWSUI::Size available) override { return {available.width, 60}; }
        void OnArrange() override
        {
            if (arranged)
                arranged();
        }
        void OnRender(LWSUI::Canvas&) override { ++paints; }
    };
}  // namespace
TEST_CASE("Layout changes during arrangement discard earlier width corrections",
          "[ui][resize-jitter][layout][menu]")
{
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    LWS::PlatformContext platform;
    REQUIRE(platform.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    LWS::Window window(platform);
    REQUIRE(window.Create({.clientSize = {640, 480}, .visible = true}) == LWS::Result::Success);
    LWSUI::UIHost host(window);
    const bool liveResize = GENERATE(false, true);
    host.SetRedrawOnResize(liveResize);
    auto root = std::make_unique<LayoutChangeProbe>();
    auto* probe = root.get();
    host.SetRoot(std::move(root));
    auto menu = std::make_unique<LWSUI::MenuBar>(
        std::vector<LWSUI::MenuBarItem>{{"File", {}}, {"Edit", {}}});
    auto* bar = menu.get();
    bar->orientation = LWSUI::Orientation::Vertical;
    host.SetMainMenu(std::move(menu), LWSUI::MenuDock::Left);
    bool changed = false;
    probe->arranged = [&]
    {
        if (!changed)
        {
            changed = true;
            bar->orientation = LWSUI::Orientation::Horizontal;
            host.SetMainMenuDock(LWSUI::MenuDock::Floating);
        }
    };
    host.Update();
    REQUIRE(changed);
    REQUIRE(probe->paints == 1);
    REQUIRE(bar->Bounds().width == Catch::Approx(bar->NaturalSpan()));
    const auto settled = bar->Bounds();
    host.Invalidate(true);
    host.Update();
    REQUIRE(bar->Bounds().width == settled.width);
    REQUIRE(bar->Bounds().height == settled.height);
    probe->arranged = {};
}
TEST_CASE("Windows UI rendering scale stays at DPI across client-size rounding", "[ui][resize-jitter][dpi]")
{
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    LWS::PlatformContext platform;
    REQUIRE(platform.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    LWS::Window window(platform);
    const auto uncreatedDpi = LWS::Win32::GetDpi(window);
    REQUIRE_FALSE(uncreatedDpi);
    REQUIRE(uncreatedDpi.error() == LWS::Result::InvalidState);
    REQUIRE(window.Create({.clientSize = {600, 400}, .visible = true}) == LWS::Result::Success);
    LWSUI::UIHost host(window);
    host.SetRedrawOnResize(true);
    auto content = std::make_unique<LWSUI::FlowPanel>();
    auto* flow = content.get();
    flow->AddItem(std::make_unique<IntrinsicItem>());
    flow->AddItem(std::make_unique<IntrinsicItem>());
    host.SetRoot(std::move(content));
    host.Update();
    const auto handle = LWS::Win32::GetHwnd(window);
    REQUIRE(handle);
    const auto dpi = LWS::Win32::GetDpi(window);
    REQUIRE(dpi);
    // Avoid introducing another direct import of this newer Windows API.
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    const auto nativeDpi = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (nativeDpi)
        REQUIRE(*dpi == nativeDpi(*handle));
    const auto expected = double(*dpi) / 96.0;
    for (int width : {600, 601, 602, 603, 602, 601, 600})
    {
        REQUIRE(window.RequestPlacement({.clientSize = LWS::LogicalSize{width, 400}}) == LWS::Result::Success);
        host.Update();
        const auto scale = host.DrawingScale();
        REQUIRE(scale);
        REQUIRE(scale->x == expected);
        REQUIRE(scale->y == expected);
        const auto metrics = window.GetClientAreaMetrics();
        REQUIRE(metrics.pixels);
        REQUIRE(flow->Bounds().width * scale->x == Catch::Approx(metrics.pixels->x).margin(.001));
        REQUIRE(flow->Children()[0]->Bounds().height * scale->y ==
                Catch::Approx(std::round(40 * expected)).margin(.001));
    }
}

namespace
{
    struct Raster
    {
        int width = 0, height = 0;
        std::vector<std::byte> pixels;
    };
    Raster Capture(LWSUI::Canvas& canvas)
    {
        const auto buffer = canvas.End();
        Raster result{int(buffer.width), int(buffer.height),
                      std::vector<std::byte>(size_t(buffer.width) * buffer.height * 4)};
        for (int y = 0; y < result.height; ++y)
            std::copy_n(buffer.pixels.data() + size_t(y) * buffer.rowPitch, size_t(result.width) * 4,
                        result.pixels.data() + size_t(y) * result.width * 4);
        return result;
    }
    Raster Crop(const Raster& raster, int x, int y, int width, int height)
    {
        REQUIRE(x >= 0);
        REQUIRE(y >= 0);
        REQUIRE(width > 0);
        REQUIRE(height > 0);
        REQUIRE(x + width <= raster.width);
        REQUIRE(y + height <= raster.height);
        Raster result{width, height, std::vector<std::byte>(size_t(width) * height * 4)};
        for (int row = 0; row < height; ++row)
            std::copy_n(raster.pixels.data() + (size_t(y + row) * raster.width + x) * 4, size_t(width) * 4,
                        result.pixels.data() + size_t(row) * width * 4);
        return result;
    }
    Raster InkCrop(const Raster& raster)
    {
        int left = raster.width, top = raster.height, right = -1, bottom = -1;
        for (int y = 0; y < raster.height; ++y)
            for (int x = 0; x < raster.width; ++x)
            {
                const auto offset = (size_t(y) * raster.width + x) * 4;
                if (!std::equal(raster.pixels.begin() + offset, raster.pixels.begin() + offset + 4,
                                raster.pixels.begin()))
                {
                    left = std::min(left, x);
                    top = std::min(top, y);
                    right = std::max(right, x);
                    bottom = std::max(bottom, y);
                }
            }
        REQUIRE(right >= left);
        REQUIRE(bottom >= top);
        return Crop(raster, left, top, right - left + 1, bottom - top + 1);
    }
    size_t ChangedPixels(const Raster& a, const Raster& b)
    {
        REQUIRE(a.width == b.width);
        REQUIRE(a.height == b.height);
        size_t changes = 0;
        for (size_t i = 0; i < a.pixels.size(); i += 4)
            changes += !std::equal(a.pixels.begin() + i, a.pixels.begin() + i + 4, b.pixels.begin() + i);
        return changes;
    }
    void BeginRaster(LWSUI::Canvas& canvas, int width, int height, double scale)
    {
        canvas.Begin(width, height, {scale, scale});
        // Cover edge pixels despite float representation of fractional logical dimensions.
        canvas.Fill(0, 0, float((width + 2) / scale), float((height + 2) / scale),
                    LLUtils::Color{uint32_t{0x171c25ff}});
    }
}  // namespace
TEST_CASE("Fixed controls keep identical glyph and border crops across pixel resize sweeps",
          "[ui][resize-jitter][raster]")
{
    for (const double scale : {1.0, 1.25, 1.5})
    {
        CAPTURE(scale);
        LWSUI::Canvas canvas;
        auto button = std::make_unique<LWSUI::Button>("Live resize: iii WWW 012345");
        LWSUI::Panel panel(std::move(button));
        panel.padding = LWSUI::Insets{float(4 / scale)};
        panel.border = LWSUI::Insets{float(1 / scale)};
        const auto render = [&](int framebufferWidth, int framebufferHeight)
        {
            BeginRaster(canvas, framebufferWidth, framebufferHeight, scale);
            const LWSUI::Rect bounds{float(16 / scale), float(12 / scale), float(300 / scale), float(64 / scale)};
            panel.Measure({bounds.width, bounds.height});
            panel.Arrange(bounds);
            panel.Render(canvas);
            return Crop(Capture(canvas), 16, 12, 300, 64);
        };
        const auto baseline = render(480, 160);
        for (int step = 1; step <= 12; ++step)
        {
            CAPTURE(step);
            REQUIRE(ChangedPixels(baseline, render(480 + step, 160 + step)) == 0);
        }
        for (int step = 12; step >= 0; --step)
            REQUIRE(ChangedPixels(baseline, render(480 + step, 160 + step)) == 0);
    }
}
TEST_CASE("Aligned text crops reject subpixel glyph and spacing shimmer", "[ui][resize-jitter][raster]")
{
    for (const double scale : {1.0, 1.25, 1.5})
        for (const bool centered : {false, true})
        {
            CAPTURE(scale, centered);
            LWSUI::Canvas canvas;
            const auto render = [&](double phase, int framebufferWidth)
            {
                BeginRaster(canvas, framebufferWidth, 160, scale);
                const float x = float((40 + phase) / scale), y = float((20 + phase) / scale);
                const auto color = LLUtils::Color{uint32_t{0xe5eaf2ff}};
                if (centered)
                    canvas.CenteredText("Resize: iii WWW 012345", x, y, float(400 / scale), float(72 / scale), color,
                                        LWSUI::FontSpec{16});
                else
                    canvas.Text("Resize: iii WWW 012345", x, y, float(400 / scale), float(72 / scale), color,
                                LWSUI::FontSpec{16});
                // Align by the ink rectangle so intended whole-pixel translation is allowed.
                // Differences inside that rectangle expose changed glyph shape or spacing.
                return InkCrop(Capture(canvas));
            };
            const auto baseline = render(.01, 600);
            int width = 600;
            for (const double phase : {.2, .49, .51, .8, .99})
            {
                CAPTURE(phase);
                REQUIRE(ChangedPixels(baseline, render(phase, ++width)) == 0);
            }
        }
}
TEST_CASE("Returning to a framebuffer size restores identical geometry and pixels", "[ui][resize-jitter][raster]")
{
    for (const double scale : {1.0, 1.25, 1.5})
    {
        CAPTURE(scale);
        LWSUI::Canvas canvas;
        LWSUI::Button button("Centered resize sample");
        const auto render = [&](int width, int height)
        {
            BeginRaster(canvas, width, height, scale);
            const LWSUI::Rect bounds{float((width - 300) / (2 * scale)), float((height - 64) / (2 * scale)),
                                     float(300 / scale), float(64 / scale)};
            button.Measure({bounds.width, bounds.height});
            button.Arrange(bounds);
            button.Render(canvas);
            return Capture(canvas);
        };
        const auto original = render(500, 180);
        const auto originalBounds = button.Bounds();
        render(501, 181);
        render(507, 187);
        render(499, 179);
        const auto restored = render(500, 180);
        REQUIRE(button.Bounds().x == originalBounds.x);
        REQUIRE(button.Bounds().y == originalBounds.y);
        REQUIRE(button.Bounds().width == originalBounds.width);
        REQUIRE(button.Bounds().height == originalBounds.height);
        REQUIRE(ChangedPixels(original, restored) == 0);
    }
}
TEST_CASE("The showcase glyph and caption cross a wrap boundary once per direction",
          "[ui][resize-jitter][flow][showcase-sample]")
{
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    LWS::PlatformContext platform;
    REQUIRE(platform.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    LWS::Window window(platform);
    REQUIRE(window.Create({.clientSize = {600, 400}, .visible = true}) == LWS::Result::Success);
    LWSUI::UIHost host(window);
    auto content = std::make_unique<LWSUI::FlowPanel>();
    auto* flow = content.get();
    auto& glyph = flow->AddItem(std::make_unique<LWSUI::GlyphButton>(LWSUI::Glyph::DoubleChevronUp));
    auto& caption = flow->AddItem(std::make_unique<LWSUI::Label>("FlowPanel wraps intrinsic controls"));
    host.SetRoot(std::move(content));
    host.Update();
    const auto scale = host.DrawingScale();
    REQUIRE(scale);
    const float threshold = glyph.MeasureContent({10000, 200}).width + flow->gap +
                            caption.MeasureContent({10000, 200}).width;
    const int thresholdPixels = int(std::ceil(threshold * scale->x));
    int transitions = 0;
    bool previous = true;
    std::array<LWSUI::Rect, 13> forward;
    for (int delta = -6; delta <= 6; ++delta)
    {
        const float allocated = float((thresholdPixels + delta) / scale->x);
        const float requested = float((thresholdPixels + delta + .3) / scale->x);
        flow->Measure({requested, 200});
        flow->Arrange({0, 0, allocated, 200});
        const bool wrapped = caption.Bounds().y > glyph.Bounds().y;
        if (wrapped != previous)
            ++transitions;
        previous = wrapped;
        forward[size_t(delta + 6)] = caption.Bounds();
        flow->Measure({requested, 200});
        flow->Arrange({0, 0, allocated, 200});
        REQUIRE(caption.Bounds().y == forward[size_t(delta + 6)].y);
    }
    REQUIRE(transitions == 1);
    transitions = 0;
    for (int delta = 6; delta >= -6; --delta)
    {
        const float allocated = float((thresholdPixels + delta) / scale->x);
        const float requested = float((thresholdPixels + delta + .3) / scale->x);
        flow->Measure({requested, 200});
        flow->Arrange({0, 0, allocated, 200});
        const bool wrapped = caption.Bounds().y > glyph.Bounds().y;
        if (wrapped != previous)
            ++transitions;
        previous = wrapped;
        REQUIRE(caption.Bounds().x == forward[size_t(delta + 6)].x);
        REQUIRE(caption.Bounds().y == forward[size_t(delta + 6)].y);
        REQUIRE(caption.Bounds().width == forward[size_t(delta + 6)].width);
        REQUIRE(caption.Bounds().height == forward[size_t(delta + 6)].height);
    }
    REQUIRE(transitions == 1);
}

TEST_CASE("Raster comparison detects distortion from rounded-client scaling", "[ui][resize-jitter][raster]")
{
    const double nominal = 1.25;
    const int framebufferWidth = 753;
    const double roundedLogicalWidth = std::round(framebufferWidth / nominal);
    const double legacyScale = framebufferWidth / roundedLogicalWidth;
    REQUIRE(legacyScale != nominal);
    LWSUI::Canvas canvas;
    const auto render = [&](double scaleX)
    {
        canvas.Begin(framebufferWidth, 160, {scaleX, nominal});
        canvas.Fill(0, 0, float((framebufferWidth + 2) / scaleX), float(162 / nominal),
                    LLUtils::Color{uint32_t{0x171c25ff}});
        canvas.Text("Resize jitter: iii WWW 0123456789", 32, 20, 600, 64, LLUtils::Color{uint32_t{0xe5eaf2ff}},
                    LWSUI::FontSpec{24});
        return InkCrop(Capture(canvas));
    };
    const auto stable = render(nominal), distorted = render(legacyScale);
    // This negative control must fail image equality even after translation is removed.
    const bool differs = stable.width != distorted.width || stable.height != distorted.height ||
                         ChangedPixels(stable, distorted) != 0;
    REQUIRE(differs);
}
    #endif
#endif
