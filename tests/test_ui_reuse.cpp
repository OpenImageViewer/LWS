#ifdef LWS_TEST_UI
    #include <catch2/catch_test_macros.hpp>
    #include <catch2/catch_approx.hpp>
    #include <LWSUI/Composites.hpp>
    #include <LWSUI/internal/ScopedFlag.hpp>
    #include <LWSUI/internal/TextPresentation.hpp>
    #include <limits>
    #include <stdexcept>
using namespace LWSUI;
TEST_CASE("Numeric drafts are typed inspections without value or event changes", "[ui][reuse][numeric]")
{
    NumericEdit<int64_t> edit({0, 100, 1});
    edit.SetValue(12);
    int notifications = 0;
    auto connection = edit.OnEdit.Connect([&](int64_t, EditPhase) { ++notifications; });
    auto* text = dynamic_cast<TextBox*>(&edit.FocusTarget());
    REQUIRE(text);
    text->SetText("42");
    REQUIRE(edit.Draft() == 42);
    REQUIRE(edit.Value() == 12);
    REQUIRE(notifications == 0);
    edit.SetValidation("Model rejected the draft");
    REQUIRE(text->Validation() == "Model rejected the draft");
    edit.SetValidation({});
    REQUIRE(text->Validation().empty());
    for (const auto* invalid : {"-", "101", "9223372036854775808"})
    {
        text->SetText(invalid);
        REQUIRE_FALSE(edit.Draft());
        REQUIRE(edit.Value() == 12);
        REQUIRE(notifications == 0);
    }
    NumericEdit<int64_t> exact;
    auto& exactText = static_cast<TextBox&>(exact.FocusTarget());
    exactText.SetText("9223372036854775807");
    REQUIRE(exact.Draft() == std::numeric_limits<int64_t>::max());
    exactText.SetText("-9223372036854775808");
    REQUIRE(exact.Draft() == std::numeric_limits<int64_t>::lowest());

    NumericEdit<double> decimal({-1., 1., .1});
    decimal.SetValue(.2);
    auto& decimalText = static_cast<TextBox&>(decimal.FocusTarget());
    decimalText.SetText("0.75");
    REQUIRE(decimal.Draft() == .75);
    for (const auto* invalid : {"nan", "inf", "1.1", "unfinished"})
    {
        decimalText.SetText(invalid);
        REQUIRE_FALSE(decimal.Draft());
        REQUIRE(decimal.Value() == .2);
    }
}
TEST_CASE("Shared numeric rules retain numeric and slider boundary differences", "[ui][reuse][numeric]")
{
    NumericSpec<int64_t> unbounded;
    REQUIRE(ValidNumericSpec(unbounded));
    REQUIRE(ValidNumericValue(std::numeric_limits<int64_t>::lowest(), unbounded));
    REQUIRE(ValidNumericValue(std::numeric_limits<int64_t>::max(), unbounded));
    const NumericSpec<int64_t> fixed{5, 5, 1};
    REQUIRE(ValidNumericSpec(fixed));
    REQUIRE(ValidNumericValue(int64_t{5}, fixed));
    REQUIRE_FALSE(ValidNumericValue(int64_t{6}, fixed));
    REQUIRE(SliderRangeError(fixed));
    NumericEdit<int64_t> fixedEdit(fixed);
    REQUIRE(fixedEdit.Value() == 5);
    REQUIRE_FALSE(ValidNumericSpec(NumericSpec<int64_t>{10, 0, 1}));
    REQUIRE_FALSE(ValidNumericSpec(NumericSpec<int64_t>{0, 10, 0}));
    REQUIRE_FALSE(ValidNumericSpec(NumericSpec<double>{0., 1., std::numeric_limits<double>::quiet_NaN()}));
    REQUIRE_FALSE(ValidNumericSpec(NumericSpec<double>{0., std::numeric_limits<double>::infinity(), .1}));
    REQUIRE_FALSE(ValidNumericValue(std::numeric_limits<double>::quiet_NaN(), NumericSpec<double>{}));
    REQUIRE_FALSE(ValidNumericValue(std::numeric_limits<double>::infinity(), NumericSpec<double>{}));
    REQUIRE(SliderRangeError(NumericSpec<int64_t>{0, std::numeric_limits<int64_t>::max(), 1}));
    REQUIRE(SliderRangeError(NumericSpec<double>{0., std::numeric_limits<double>::max(), .1}));
}
TEST_CASE("Scoped flags restore nested state during exception unwinding", "[ui][reuse][state]")
{
    bool flag = false;
    {
        const internal::ScopedFlag outer(flag, true);
        REQUIRE(flag);
        try
        {
            const internal::ScopedFlag inner(flag, false);
            REQUIRE_FALSE(flag);
            throw std::runtime_error("callback failed");
        }
        catch (const std::runtime_error&)
        {
            REQUIRE(flag);
        }
        REQUIRE(flag);
    }
    REQUIRE_FALSE(flag);
    flag = true;
    {
        const internal::ScopedFlag unchanged(flag, true);
        REQUIRE(flag);
    }
    REQUIRE(flag);
}
TEST_CASE("Shared text presentation keeps native alignment and UTF-8 trimming at fractional scales",
          "[ui][reuse][text]")
{
    for (const double scale : {1., 1.25, 1.5})
    {
        CAPTURE(scale);
        Canvas canvas;
        canvas.Begin(1000, 300, {scale, scale});
        const FontSpec font{16};
        const Rect area{10, 20, 200, 60};
        const auto centered = internal::AlignedTextBounds(canvas, "Native text", area, font,
                                                          Alignment::Center, Alignment::Center);
        REQUIRE(centered.x > area.x);
        REQUIRE(centered.y > area.y);
        REQUIRE(centered.height == Catch::Approx(canvas.TextHeight("Native text", area.width, font, false)));
        REQUIRE(centered.y + centered.height / 2 == Catch::Approx(area.y + area.height / 2));
        const auto right = internal::AlignedTextBounds(canvas, "Native text", area, font,
                                                       Alignment::End, Alignment::End);
        REQUIRE(right.x + right.width == Catch::Approx(area.x + area.width));
        REQUIRE(right.width == Catch::Approx(canvas.Measure("Native text", font)));
        REQUIRE(right.y + right.height == Catch::Approx(area.y + area.height));
        const auto wrapped = internal::AlignedTextBounds(canvas, "Several words must wrap into more than one line",
                                                         {0, 0, 70, 200}, font, Alignment::Start,
                                                         Alignment::Start, true);
        REQUIRE(wrapped.height > centered.height);
        REQUIRE(wrapped.width == 70);
        const auto narrow = internal::AlignedTextBounds(canvas, "Long text", {3, 4, 0, 10}, font,
                                                        Alignment::Center, Alignment::Center);
        REQUIRE(narrow.width == 0);
        REQUIRE(narrow.height == 0);
        REQUIRE(internal::EllipsizeText(canvas, "Short", 200, font) == "Short");
        REQUIRE(internal::EllipsizeText(canvas, "Long text", 0, font).empty());
        const std::string text = "\xc3\xa9\xf0\x9f\x99\x82 repeated Unicode text";
        const std::string dots = "\xe2\x80\xa6";
        const float limit = canvas.Measure(text.substr(0, 6) + dots, font);
        const auto trimmed = internal::EllipsizeText(canvas, text, limit, font);
        REQUIRE(trimmed.ends_with(dots));
        const auto prefix = trimmed.substr(0, trimmed.size() - dots.size());
        REQUIRE(text.starts_with(prefix));
        REQUIRE((static_cast<unsigned char>(text[prefix.size()]) & 0xc0) != 0x80);
        REQUIRE(canvas.Measure(trimmed, font) <= limit + .001f);
        REQUIRE(internal::EllipsizeText(canvas, text, canvas.Measure(dots, font) / 2, font).empty());
        canvas.End();
    }
}
#endif
