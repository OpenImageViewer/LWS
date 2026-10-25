#ifdef LWS_TEST_UI
    #include <catch2/catch_test_macros.hpp>
    #include <catch2/catch_approx.hpp>
    #include <LWSUI/Composites.hpp>
    #include <LWSUI/UIHost.hpp>
    #include <LWSUI/internal/ColorTextBox.hpp>
    #include "../src/LWSUI/source/ColorPanel.hpp"
    #include "../src/LWSUI/source/MenuHost.hpp"
    #include <LWS/TextClipboard.hpp>
    #include <LWS/Platform.hpp>
    #ifdef LWS_HAS_WIN32_BACKEND
        #include <LWS/Win32/Platform.hpp>
    #endif
    #include <chrono>
    #include <cctype>
    #include <thread>

namespace
{
    using namespace LWSUI;
    using K = LWS::KeyCode;
    struct TextEnvironment
    {
        LWS::PlatformContext context;
        std::unique_ptr<LWS::Window> window;
        std::unique_ptr<UIHost> host;
        TextBox* text;
        Button* next;
        explicit TextEnvironment(std::string value, Size size = {260, 100}, TextBoxMode mode = TextBoxMode::Multiline)
        {
    #ifdef LWS_HAS_WIN32_BACKEND
            REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
            REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    #else
            REQUIRE(context.Init({.backend = LWS::BackendId::Wayland}) == LWS::Result::Success);
    #endif
            window = std::make_unique<LWS::Window>(context);
            REQUIRE(window->Create({.clientSize = {400, 300}}) == LWS::Result::Success);
            host = std::make_unique<UIHost>(*window);
            auto stack = std::make_unique<StackPanel>();
            text = &stack->Emplace<TextBox>(std::move(value), mode);
            next = &stack->Emplace<Button>("Next");
            host->SetRoot(std::move(stack));
            host->Root()->Measure({400, 300});
            host->Root()->Arrange({0, 0, 400, 300});
            text->Arrange({10, 10, size.width, size.height});
            REQUIRE(host->Focus(text));
        }
        bool Key(K key, bool control = false, bool shift = false)
        {
            return host->Route({.kind = InputKind::KeyDown, .key = key, .control = control, .shift = shift});
        }
        void Type(std::string value) { REQUIRE(host->Route({.kind = InputKind::Text, .text = std::move(value)})); }
        ScrollBar& Bar() { return static_cast<ScrollBar&>(*text->Children().front()); }
        float Width() const
        {
            return text->Bounds().width - 2 * text->Style().textPadding -
                   (text->Children().front()->Visible() ? text->Style().scrollbarWidth + text->Style().scrollbarMargin
                                                        : 0);
        }
        void Pump(int milliseconds)
        {
            const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
            do
            {
                REQUIRE(context.ProcessMessages() == LWS::LoopResult::Continue);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            } while (std::chrono::steady_clock::now() < until);
        }
    };
    std::string Paragraphs()
    {
        std::string result;
        for (int i = 0; i < 40; ++i)
            result += "Line " + std::to_string(i) + " with some words\n";
        return result;
    }
}  // namespace

TEST_CASE("Wrapped text geometry preserves UTF-8 lines and caret affinity", "[ui][text][layout]")
{
    Canvas canvas;
    const FontSpec font{16};
    const std::string text = "a\n\n\xc3\xa9\xf0\x9f\x98\x80\n";
    const auto lines = canvas.TextLines(text, 200, font);
    REQUIRE(lines.size() == 4);
    REQUIRE(lines[0].range == TextRange{0, 1});
    REQUIRE(lines[1].range == TextRange{2, 0});
    REQUIRE(lines[2].range == TextRange{3, 6});
    REQUIRE(lines[3].range == TextRange{text.size(), 0});
    for (const auto& line : lines)
    {
        REQUIRE(line.height > 0);
        const auto caret = canvas.CaretBounds(text, {line.range.offset}, 200, font);
        REQUIRE(caret.y == Catch::Approx(line.y));
        REQUIRE(caret.height > 0);
        const auto hit = canvas.HitTestText(text, 0, line.y + line.height / 2, 200, font);
        REQUIRE(hit.offset == line.range.offset);
    }
    const std::string wrapped = "alpha beta gamma delta epsilon zeta";
    const auto visual = canvas.TextLines(wrapped, 75, font);
    REQUIRE(visual.size() > 1);
    const auto boundary = visual[1].range.offset;
    const auto before = canvas.CaretBounds(wrapped, {boundary, true}, 75, font);
    const auto after = canvas.CaretBounds(wrapped, {boundary, false}, 75, font);
    REQUIRE(before.y < after.y);
    REQUIRE(canvas.TextLines(wrapped, 600, font).size() == 1);
    REQUIRE(canvas.TextLines("", 75, font).size() == 1);
}

TEST_CASE("Multiline RTL caret and hit testing share the rendered origin", "[ui][text][layout]")
{
    Canvas canvas;
    const FontSpec font{20};
    const std::string text = "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d\n\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d";
    // Interior bidi boundaries are unambiguous; a paragraph endpoint can share a newline coordinate.
    for (size_t offset : {size_t(2), size_t(4), size_t(6), size_t(11), size_t(13), size_t(15)})
    {
        const auto caret = canvas.CaretBounds(text, {offset}, 300, font);
        const auto hit = canvas.HitTestText(text, caret.x, caret.y + caret.height / 2, 300, font);
        REQUIRE(hit.offset == offset);
    }
}

TEST_CASE("Multiline text retains normalization and transactional editing", "[ui][text][multiline]")
{
    TextEnvironment env("one\r\ntwo\rthree\n");
    REQUIRE(env.text->Mode() == TextBoxMode::Multiline);
    REQUIRE(env.text->Text() == "one\ntwo\nthree\n");
    env.text->SetText("one");
    unsigned previews = 0, commits = 0, cancels = 0;
    auto edited = env.text->OnEdit.Connect(
        [&](const auto&, EditPhase phase)
        {
            previews += phase == EditPhase::Preview;
            commits += phase == EditPhase::Commit;
            cancels += phase == EditPhase::Cancel;
        });
    REQUIRE(env.Key(K::End, true));
    REQUIRE(env.Key(K::Enter));
    env.Type("two\r\nthree");
    REQUIRE(env.text->Text() == "one\ntwo\nthree");
    REQUIRE(commits == 0);
    env.host->SetInvalidEditPolicy(InvalidEditPolicy::BlockOnLeave);
    env.text->SetValidation("rejected");
    REQUIRE_FALSE(env.text->Dispatch({.kind = InputKind::KeyDown, .key = K::Enter, .control = true}));
    REQUIRE_FALSE(env.host->Focus(env.next));
    REQUIRE(env.Key(K::Escape));
    REQUIRE(env.text->Text() == "one");
    REQUIRE(cancels == 1);
    env.Type("\nkept");
    REQUIRE(env.Key(K::Enter, true));
    REQUIRE(commits == 1);
    REQUIRE(previews == 3);
    env.text->SetReadOnly(true);
    REQUIRE(env.Key(K::Enter));
    env.Type("ignored");
    REQUIRE(env.text->Text() == "one\nkept");
}

TEST_CASE("Multiline navigation preserves columns and visual line boundaries", "[ui][text][multiline]")
{
    TextEnvironment env("abcd\nx\nabcd", {250, 200});
    REQUIRE(env.Key(K::Home, true));
    for (int i = 0; i < 3; ++i)
        REQUIRE(env.Key(K::Right));
    REQUIRE(env.Key(K::Down));
    REQUIRE(env.Key(K::Down));
    env.Type("!");
    REQUIRE(env.text->Text() == "abcd\nx\nabc!d");
    env.text->SetText("alpha\nbeta");
    REQUIRE(env.Key(K::Home, true));
    REQUIRE(env.Key(K::Right));
    REQUIRE(env.Key(K::Right));
    REQUIRE(env.Key(K::End, true, true));
    env.Type("X");
    REQUIRE(env.text->Text() == "alX");
    env.text->SetText("alpha beta gamma delta epsilon zeta");
    env.text->Arrange({10, 10, 95, 250});
    REQUIRE(env.Key(K::Home, true));
    const auto lines = env.host->MeasureTextLines(env.text->Text(), env.Width(), env.text->Font());
    REQUIRE(lines.size() > 1);
    REQUIRE(env.Key(K::End));
    env.Type("!");
    auto expected = std::string("alpha beta gamma delta epsilon zeta");
    expected.insert(lines[0].range.length, "!");
    REQUIRE(env.text->Text() == expected);
    env.text->SetText("a\n\xc3\xa9\xf0\x9f\x98\x80");
    REQUIRE(env.Key(K::End, true));
    REQUIRE(env.Key(K::Backspace));
    REQUIRE(env.text->Text() == "a\n\xc3\xa9");
}

TEST_CASE("Multiline mouse hit testing follows width and font changes before paint", "[ui][text][multiline]")
{
    TextEnvironment env("first\nsecond\nthird", {220, 150});
    auto theme = env.text->Style();
    theme.font.size = 24;
    env.text->SetStyle(theme);
    env.text->Arrange({20, 15, 180, 200});
    const auto lines = env.host->MeasureTextLines(env.text->Text(), env.Width(), env.text->Font());
    const auto b = env.text->Bounds();
    const float x = b.x + theme.textPadding;
    const float y = b.y + theme.textPadding + lines[1].y + lines[1].height / 2;
    REQUIRE(env.host->Route({.kind = InputKind::Down, .x = x, .y = y}));
    REQUIRE(env.host->Route({.kind = InputKind::Up, .x = x, .y = y}));
    env.Type("X");
    REQUIRE(env.text->Text() == "first\nXsecond\nthird");
    Canvas canvas;
    canvas.Begin(400, 300, {1.5, 1.5});
    env.text->Render(canvas);
    REQUIRE(canvas.End().width == 400);
    env.text->SetVisibleLines(1);
    const auto one = env.text->Measure({300, 1000}).height;
    env.text->SetVisibleLines(4);
    REQUIRE(env.text->Measure({300, 1000}).height > one * 2);
}

TEST_CASE("Multiline scrolling preserves drafts and one Tab stop", "[ui][text][multiline]")
{
    TextEnvironment env(Paragraphs());
    unsigned commits = 0;
    auto edited = env.text->OnEdit.Connect([&](const auto&, EditPhase phase)
                                           { commits += phase == EditPhase::Commit; });
    REQUIRE(env.Bar().Visible());
    REQUIRE(env.Key(K::Home, true));
    env.Type("draft ");
    const auto draft = env.text->Text();
    REQUIRE(env.host->Route({.kind = InputKind::Wheel, .x = 25, .y = 25, .wheel = -1}));
    REQUIRE(env.Bar().Value() > 0);
    const auto bar = env.Bar().Bounds();
    REQUIRE(env.host->Route({.kind = InputKind::Down, .x = bar.x + 2, .y = bar.y + bar.height / 2}));
    REQUIRE(env.host->Route({.kind = InputKind::Move, .x = bar.x + 2, .y = bar.y + bar.height}));
    REQUIRE(env.host->Route({.kind = InputKind::Up, .x = bar.x + 2, .y = bar.y + bar.height}));
    REQUIRE(commits == 0);
    REQUIRE(env.host->IsFocused(*env.text));
    REQUIRE(env.text->Text() == draft);
    REQUIRE(env.Key(K::Home, true));
    REQUIRE(env.Bar().Value() == 0);
    REQUIRE(env.Key(K::PageDown));
    REQUIRE(env.Bar().Value() > 0);
    REQUIRE(env.Key(K::End, true));
    REQUIRE(env.Bar().Value() == Catch::Approx(1));
    REQUIRE(env.Key(K::Tab));
    REQUIRE(env.host->IsFocused(*env.next));
    REQUIRE(commits == 1);
    REQUIRE(env.Key(K::Tab, false, true));
    REQUIRE(env.host->IsFocused(*env.text));
}

TEST_CASE("Multiline selection auto-scroll stops on release and detach", "[ui][text][multiline]")
{
    TextEnvironment env(Paragraphs());
    REQUIRE(env.Key(K::Home, true));
    const auto b = env.text->Bounds();
    REQUIRE(env.host->Route({.kind = InputKind::Down, .x = b.x + 8, .y = b.y + 8}));
    REQUIRE(env.host->Route({.kind = InputKind::Move, .x = b.x + 8, .y = b.y + b.height + 25}));
    const auto first = env.Bar().Value();
    env.Pump(55);
    REQUIRE(env.Bar().Value() > first);
    SECTION("release")
    {
        REQUIRE(env.host->Route({.kind = InputKind::Up, .x = b.x + 8, .y = b.y + b.height + 25}));
        const auto stopped = env.Bar().Value();
        env.Pump(40);
        REQUIRE(env.Bar().Value() == stopped);
    }
    SECTION("detach")
    {
        const auto handle = env.text->Handle();
        env.host->SetRoot(nullptr);
        env.Pump(40);
        REQUIRE_FALSE(handle);
    }
}
TEST_CASE("A detached multiline editor releases its original platform timer", "[ui][text][multiline][lifetime]")
{
    std::unique_ptr<Control> retained;
    {
        TextEnvironment env(Paragraphs());
        const auto b = env.text->Bounds();
        REQUIRE(env.host->Route({.kind = InputKind::Down, .x = b.x + 8, .y = b.y + 8}));
        REQUIRE(env.host->Route({.kind = InputKind::Move, .x = b.x + 8, .y = b.y + b.height + 25}));
        retained = static_cast<Container*>(env.host->Root())->Remove(*env.text);
        REQUIRE(retained != nullptr);
    }
    TextEnvironment replacement("");
    replacement.text = static_cast<TextBox*>(retained.get());
    replacement.host->SetRoot(std::move(retained));
    replacement.text->Measure({260, 100});
    replacement.text->Arrange({10, 10, 260, 100});
    REQUIRE(replacement.host->Focus(replacement.text));
    REQUIRE(replacement.Key(K::Home, true));
    REQUIRE(replacement.host->Route({.kind = InputKind::Down, .x = 18, .y = 18}));
    REQUIRE(replacement.host->Route({.kind = InputKind::Move, .x = 18, .y = 140}));
    const auto before = replacement.Bar().Value();
    // Scheduling can exceed one 40 ms interval during a busy full-suite run. Wait for a real tick.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    do
    {
        replacement.Pump(10);
    } while (replacement.Bar().Value() == before && std::chrono::steady_clock::now() < deadline);
    REQUIRE(replacement.Bar().Value() > before);
    REQUIRE(replacement.host->Route({.kind = InputKind::Up, .x = 18, .y = 140}));
}

    #ifdef LWS_HAS_WIN32_BACKEND
TEST_CASE("Multiline clipboard paste normalizes paragraphs and copy retains selection",
          "[ui][text][multiline][clipboard]")
{
    TextEnvironment env("");
    const auto result = LWS::SetClipboardText(*env.window, "first\r\nsecond\rthird");
    if (result == LWS::ClipboardResult::AccessDenied)
        SKIP("Windows clipboard access is unavailable");
    REQUIRE(result == LWS::ClipboardResult::Success);
    REQUIRE(env.Key(K::V, true));
    REQUIRE(env.text->Text() == "first\nsecond\nthird");
    REQUIRE(env.Key(K::Enter, true));
    env.text->SetReadOnly(true);
    REQUIRE(env.Key(K::A, true));
    REQUIRE(env.Key(K::C, true));
    std::string copied;
    LWS::RequestClipboardText(*env.window,
                              [&](auto status, std::string value)
                              {
                                  REQUIRE(status == LWS::ClipboardResult::Success);
                                  copied = std::move(value);
                              });
    REQUIRE(copied == "first\nsecond\nthird");
}
    #endif

TEST_CASE("TextBox filters reject whole edits without changing the draft or selection", "[ui][text][filter]")
{
    TextBox text("12");
    text.SetText("12");
    text.SetInputFilter(InputFilters::Number(true));
    unsigned previews = 0;
    auto edited = text.OnEdit.Connect([&](const auto&, EditPhase phase) { previews += phase == EditPhase::Preview; });
    auto key = [&](K k, bool ctrl = false, bool shift = false)
    { REQUIRE(text.Dispatch({.kind = InputKind::KeyDown, .key = k, .control = ctrl, .shift = shift})); };
    key(K::A, true);
    text.SetValidation("existing validation");
    REQUIRE(text.Dispatch({.kind = InputKind::Text, .text = "3x"}));
    REQUIRE(text.Text() == "12");
    REQUIRE(text.Validation() == "existing validation");
    REQUIRE_FALSE(text.InputError().empty());
    REQUIRE(previews == 0);
    REQUIRE(text.Dispatch({.kind = InputKind::Text, .text = "34"}));
    REQUIRE(text.Text() == "34");
    REQUIRE(text.InputError().empty());
    REQUIRE(previews == 1);
    REQUIRE(text.Finish(EditPhase::Cancel));
    REQUIRE(text.Text() == "12");
    text.SetInputFilter([](auto, auto) -> std::expected<void, std::string> { return std::unexpected("No edits"); });
    key(K::Backspace);
    REQUIRE(text.Text() == "12");
    text.SetInputFilter({});
    REQUIRE(text.Dispatch({.kind = InputKind::Text, .text = "X"}));
    REQUIRE(text.Text() == "12X");
    REQUIRE(text.Finish(EditPhase::Commit));
    text.SetInputFilter(InputFilters::Number(true));
    REQUIRE(text.Dispatch({.kind = InputKind::Text, .text = "!"}));
    REQUIRE(text.Finish(EditPhase::Commit));
    REQUIRE(text.InputError().empty());
    text.SetText("programmatic value");
    REQUIRE(text.Text() == "programmatic value");
}

TEST_CASE("Numeric filters accept partial drafts and allow deletion repairs", "[ui][text][filter]")
{
    auto number = InputFilters::Number();
    for (auto draft : {"", "-", ".", "-.", "1.", "1e", "1e-", "1e+", "-1.2E-3"})
        REQUIRE(number(draft, TextEditOrigin::Typing));
    for (auto invalid : {"x", "1..", "1e2e3", "--1", "1e.2", "1e--", "1 2"})
        REQUIRE_FALSE(number(invalid, TextEditOrigin::Paste));
    REQUIRE(number("e-", TextEditOrigin::Delete));
    REQUIRE_FALSE(InputFilters::Number(true, false)("-1", TextEditOrigin::Typing));
    REQUIRE_FALSE(InputFilters::Number(false, false, false)("1e2", TextEditOrigin::Typing));
    REQUIRE(InputFilters::Hexadecimal(true)("#a0FF", TextEditOrigin::Typing));
    REQUIRE_FALSE(InputFilters::Hexadecimal(true)("#001122334", TextEditOrigin::Paste));
    NumericEdit<int> numeric;
    auto& field = static_cast<TextBox&>(numeric.FocusTarget());
    REQUIRE(field.Dispatch({.kind = InputKind::KeyDown, .key = K::A, .control = true}));
    REQUIRE(field.Dispatch({.kind = InputKind::Text, .text = "bad"}));
    REQUIRE(field.Text() == "0");
    REQUIRE(numeric.Draft() == 0);
}

TEST_CASE("Fixed masks insert literals and clear slots without shifting fields", "[ui][text][mask]")
{
    TextBox text;
    REQUIRE(text.SetInputMask("AA-0000"));
    auto type = [&](std::string value) { REQUIRE(text.Dispatch({.kind = InputKind::Text, .text = std::move(value)})); };
    auto key = [&](K k, bool ctrl = false)
    { REQUIRE(text.Dispatch({.kind = InputKind::KeyDown, .key = k, .control = ctrl})); };
    REQUIRE(text.Text().empty());
    type("aB12");
    REQUIRE(text.Text() == "aB-12");
    REQUIRE(text.UnmaskedText() == "aB12");
    REQUIRE_FALSE(text.MaskComplete());
    REQUIRE_FALSE(text.Finish(EditPhase::Commit));
    type("34");
    REQUIRE(text.Text() == "aB-1234");
    REQUIRE(text.MaskComplete());
    REQUIRE(text.Finish(EditPhase::Commit));
    key(K::Home);
    key(K::Right);
    key(K::Delete);
    REQUIRE(text.Text() == "a-1234");
    type("Z");
    REQUIRE(text.Text() == "aZ-1234");
    key(K::Backspace);
    type("Q");
    REQUIRE(text.Text() == "aQ-1234");
    key(K::A, true);
    type("12-invalid");
    REQUIRE(text.Text() == "aQ-1234");
    type("xy\n9876");
    REQUIRE(text.Text() == "aQ-1234");
    type("xy-9876");
    REQUIRE(text.Text() == "xy-9876");
    REQUIRE(text.Finish(EditPhase::Cancel));
    REQUIRE(text.Text() == "aB-1234");
    text.SetText("xy0001");
    REQUIRE(text.Text() == "xy-0001");
    text.SetText("wrong");
    REQUIRE(text.Text() == "xy-0001");
    REQUIRE_FALSE(text.SetInputMask("00:00"));
    REQUIRE(text.Text() == "xy-0001");
    REQUIRE(text.SetInputMask(""));
    REQUIRE(text.Text() == "xy-0001");
    TextBox phone;
    REQUIRE(phone.SetInputMask("(000) 000-0000"));
    phone.SetText("(123) 456-7890");
    REQUIRE(phone.Text() == "(123) 456-7890");
    phone.SetText("");
    REQUIRE(phone.Text().empty());
    REQUIRE(phone.Dispatch({.kind = InputKind::Text, .text = "1234567890"}));
    REQUIRE(phone.UnmaskedText() == "1234567890");
    REQUIRE_FALSE(phone.SetInputMask("000\\"));
    REQUIRE_FALSE(phone.SetInputMask("___"));
    TextBox multiline({}, TextBoxMode::Multiline);
    REQUIRE_FALSE(multiline.SetInputMask("00"));
}

TEST_CASE("Rejected TextBox input paints the error underline", "[ui][text][filter][render]")
{
    TextEnvironment env("12", {260, 100}, TextBoxMode::SingleLine);
    env.text->SetInputFilter(InputFilters::Number(true));
    env.Type("invalid");
    for (bool borderless : {false, true})
    {
        env.text->SetBorderless(borderless);
        Canvas canvas;
        canvas.Begin(400, 300, {1, 1});
        env.text->Render(canvas);
        const auto bitmap = canvas.End();
        const auto b = env.text->Bounds();
        const size_t offset = size_t(b.y + b.height - 1) * bitmap.rowPitch + size_t(b.x + 20) * 4;
        REQUIRE(std::to_integer<unsigned>(bitmap.pixels[offset + 2]) >
                std::to_integer<unsigned>(bitmap.pixels[offset + 1]) * 2);
    }
}

    #ifdef LWS_HAS_WIN32_BACKEND
TEST_CASE("Filtered mask clipboard edits are atomic and omit slot placeholders", "[ui][text][mask][clipboard]")
{
    TextEnvironment env("", {260, 100}, TextBoxMode::SingleLine);
    REQUIRE(env.text->SetInputMask("00:00"));
    auto result = LWS::SetClipboardText(*env.window, "12:34");
    if (result == LWS::ClipboardResult::AccessDenied)
        SKIP("Windows clipboard access is unavailable");
    REQUIRE(result == LWS::ClipboardResult::Success);
    REQUIRE(env.Key(K::V, true));
    REQUIRE(env.text->Text() == "12:34");
    REQUIRE(env.Key(K::A, true));
    REQUIRE(LWS::SetClipboardText(*env.window, "12-34") == LWS::ClipboardResult::Success);
    REQUIRE(env.Key(K::V, true));
    REQUIRE(env.text->Text() == "12:34");
    env.text->SetInputFilter(
        [](auto, auto origin) -> std::expected<void, std::string>
        {
            if (origin == TextEditOrigin::Cut)
                return std::unexpected("Cut disabled");
            return {};
        });
    REQUIRE(env.Key(K::X, true));
    REQUIRE(env.text->Text() == "12:34");
    std::string copied;
    LWS::RequestClipboardText(*env.window, [&](auto, auto value) { copied = std::move(value); });
    if (copied.empty())
        SKIP("Windows clipboard read access is unavailable");
    REQUIRE(copied == "12-34");
    env.text->SetInputFilter({});
    REQUIRE(env.Key(K::Home));
    REQUIRE(env.Key(K::Delete));
    REQUIRE(env.Key(K::A, true));
    // Clipboard observers can briefly lock the clipboard immediately after publication.
    for (unsigned attempt = 0; attempt < 20; ++attempt)
    {
        REQUIRE(env.Key(K::C, true));
        LWS::RequestClipboardText(*env.window,
                                  [&](auto status, auto value)
                                  {
                                      if (status == LWS::ClipboardResult::Success)
                                          copied = std::move(value);
                                  });
        if (copied == "2:34")
            break;
        env.Pump(5);
    }
    REQUIRE(copied == "2:34");
    for (unsigned attempt = 0; attempt < 20 && !env.text->Text().empty(); ++attempt)
    {
        REQUIRE(env.Key(K::X, true));
        env.Pump(5);
    }
    REQUIRE(env.text->Text().empty());
}
    #endif

TEST_CASE("Hexadecimal form flags choose accepted notations and preserve draft editing", "[ui][text][filter][hex]")
{
    REQUIRE_THROWS_AS(InputFilters::Hexadecimal(HexadecimalFormFlags{HexadecimalForm::None}), std::invalid_argument);
    REQUIRE_THROWS_AS(InputFilters::Hexadecimal(HexadecimalFormFlags{static_cast<HexadecimalForm>(8)}),
                      std::invalid_argument);
    for (unsigned bits = 1; bits <= 7; ++bits)
    {
        const HexadecimalFormFlags forms{static_cast<HexadecimalForm>(bits)};
        const auto filter = InputFilters::Hexadecimal(forms);
        REQUIRE(filter("", TextEditOrigin::Typing));
        REQUIRE(bool(filter("aF09", TextEditOrigin::Paste)) == forms.test(HexadecimalForm::Bare));
        REQUIRE(bool(filter("#aF09", TextEditOrigin::Paste)) == forms.test(HexadecimalForm::HashPrefix));
        REQUIRE(bool(filter("0xaF09", TextEditOrigin::Paste)) == forms.test(HexadecimalForm::ZeroXPrefix));
        REQUIRE(bool(filter("0XaF09", TextEditOrigin::Paste)) == forms.test(HexadecimalForm::ZeroXPrefix));
        REQUIRE(bool(filter("#", TextEditOrigin::Typing)) == forms.test(HexadecimalForm::HashPrefix));
        REQUIRE(bool(filter("0x", TextEditOrigin::Typing)) == forms.test(HexadecimalForm::ZeroXPrefix));
        REQUIRE(bool(filter("0", TextEditOrigin::Typing)) ==
                (forms.test(HexadecimalForm::Bare) || forms.test(HexadecimalForm::ZeroXPrefix)));
        for (auto invalid : {"#0xFF", "0x#FF", "0x0xFF", "F#F", "FF FF", "FF\n", "0xGG", "#-1", "\xc3\xa9"})
            REQUIRE_FALSE(filter(invalid, TextEditOrigin::Paste));
        REQUIRE_FALSE(internal::CheckHexadecimal("", forms, true));
        REQUIRE_FALSE(internal::CheckHexadecimal("#", forms, true));
        REQUIRE_FALSE(internal::CheckHexadecimal("0x", forms, true));
        REQUIRE(bool(internal::CheckHexadecimal("0", forms, true)) == forms.test(HexadecimalForm::Bare));
    }
    const HexadecimalFormFlags prefixes{HexadecimalForm::HashPrefix | HexadecimalForm::ZeroXPrefix};
    TextBox text("#aB");
    text.SetText("#aB");
    text.SetInputFilter(InputFilters::Hexadecimal(prefixes));
    auto validator = text.OnEdit.Connect(
        [&](const auto& value, EditPhase)
        {
            const auto result = internal::CheckHexadecimal(value, prefixes, true);
            text.SetValidation(result ? "" : result.error());
        });
    REQUIRE(text.Dispatch({.kind = InputKind::KeyDown, .key = K::A, .control = true}));
    REQUIRE(text.Dispatch({.kind = InputKind::Text, .text = "0xGG"}));
    REQUIRE(text.Text() == "#aB");
    REQUIRE_FALSE(text.InputError().empty());
    REQUIRE(text.Dispatch({.kind = InputKind::Text, .text = "0"}));
    REQUIRE_FALSE(text.Finish(EditPhase::Commit));
    REQUIRE(text.Dispatch({.kind = InputKind::Text, .text = "x"}));
    REQUIRE_FALSE(text.Finish(EditPhase::Commit));
    REQUIRE(text.Dispatch({.kind = InputKind::Text, .text = "aF"}));
    REQUIRE(text.Text() == "0xaF");
    REQUIRE(text.Finish(EditPhase::Commit));
    const auto color = InputFilters::Hexadecimal(true);
    REQUIRE(color("", TextEditOrigin::Typing));
    REQUIRE(color("#", TextEditOrigin::Typing));
    REQUIRE(color("#aAbBcC12", TextEditOrigin::Paste));
    REQUIRE_FALSE(color("0xaAbBcC", TextEditOrigin::Paste));
    REQUIRE_FALSE(color("aAbBcC", TextEditOrigin::Paste));
    REQUIRE_FALSE(color("#aAbBcC123", TextEditOrigin::Paste));
    REQUIRE_FALSE(InputFilters::Hexadecimal()("#FF", TextEditOrigin::Paste));
}


TEST_CASE("Hexadecimal spacing and group size preserve compact and editable input", "[ui][text][filter][hex]")
{
    const HexadecimalFormFlags forms{HexadecimalForm::HashPrefix | HexadecimalForm::ZeroXPrefix};
    REQUIRE_THROWS_AS(InputFilters::Hexadecimal(forms, 1, 0), std::invalid_argument);
    REQUIRE_THROWS_AS(InputFilters::Hexadecimal(false, 1, 0), std::invalid_argument);
    const auto complete = [&](std::string_view text, unsigned spacing, unsigned group)
    { return bool(internal::CheckHexadecimal(text, forms, true, spacing, group)); };
    REQUIRE(complete("#aabbccdd", 1, 2));
    REQUIRE(complete("#aa bb cc dd", 1, 2));
    REQUIRE(complete("0xAABB  CCDD", 2, 4));
    REQUIRE(complete("0XAABB   CCDD", 3, 4));
    for (auto invalid : {"#aa  bb", "#a bb", "#aa b", "# aa bb", "#aa bb ", "#aa\tbb"})
        REQUIRE_FALSE(complete(invalid, 1, 2));
    REQUIRE_FALSE(complete("0xAABB CCDD", 2, 4));
    REQUIRE_FALSE(complete("0xAA  BB", 2, 4));
    REQUIRE_FALSE(complete("#aa bb", 0, 2));
    REQUIRE_FALSE(InputFilters::Hexadecimal(forms)("#aa bb", TextEditOrigin::Paste));
    auto filter = InputFilters::Hexadecimal(forms, 2, 4);
    for (auto draft : {"", "#", "0", "0x", "0xAABB ", "0xAA  CCDD", "0xAABB  CC"})
        REQUIRE(filter(draft, TextEditOrigin::Typing));
    REQUIRE_FALSE(filter("0xAABB--CCDD", TextEditOrigin::Paste));
    REQUIRE(InputFilters::Hexadecimal(true, 1, 2)("#aa bb cc dd", TextEditOrigin::Paste));
    REQUIRE_FALSE(InputFilters::Hexadecimal(true, 1, 2)("#aa bb cc dd ee", TextEditOrigin::Paste));
}
TEST_CASE("Color text parses prefixed hexadecimal and variable-width decimal RGBA", "[ui][color][text]")
{
    using namespace LWSUI::internal;
    const LLUtils::Color expected{uint32_t{0xfff0d2ff}};
    for (auto value : {"#fff0d2ff", "#ff f0 d2 ff", "0xFFF0D2FF", "0Xff f0 d2 ff", "255,240,210,255", " 255 , 240, 210 ,255 "})
    {
        REQUIRE(ParseColor(value));
        REQUIRE(*ParseColor(value) == expected);
    }
    REQUIRE(*ParseColor("#fff0d2") == expected);
    REQUIRE(*ParseColor("0xff f0 d2") == expected);
    REQUIRE(*ParseColor("255,240,210") == expected);
    REQUIRE(*ParseColor("001,002,003,004") == LLUtils::Color{uint32_t{0x01020304}});
    REQUIRE(*ParseColor("0,0,0,0") == LLUtils::Color{uint32_t{0}});
    for (auto value : {"", "#", "0x", "fff0d2ff", "#f f0 d2 ff", "#ff  f0 d2 ff", "#gg ff 00 00",
                       "255,240,", "255,240,210,255,0", "256,0,0,0", "0000,0,0,0", "-1,0,0,0", "1.0,0,0,0", "1,,3,4"})
        REQUIRE_FALSE(ParseColor(value));
    REQUIRE(ColorText(expected) == "#FF-F0-D2-FF");
    REQUIRE(ColorText(expected, ColorTextFormat::ZeroX) == "0xFF-F0-D2-FF");
    REQUIRE(ColorText(expected, ColorTextFormat::DecimalRgba) == "255,240,210,255");
    auto mask = internal::DynamicMask::Create(ColorInputMask());
    REQUIRE(mask);
    for (auto value : {"", "0", "0x", "#ff-", "255,"}) REQUIRE(mask->Format(value));
    REQUIRE_FALSE(mask->Format("#0011223344"));
    REQUIRE_FALSE(mask->Format("1.0,2,3,4"));

}
TEST_CASE("ColorPicker detects both formats without rewriting drafts", "[ui][color][text]")
{
    ColorPicker picker;
    auto* text = dynamic_cast<TextBox*>(picker.Children().front().get());
    REQUIRE(text);
    picker.SetValue(LLUtils::Color{uint32_t{0x11223344}});
    REQUIRE(text->Text() == "#11-22-33-44");
    std::vector<EditPhase> phases;
    auto connection = picker.OnEdit.Connect([&](auto, auto phase) { phases.push_back(phase); });
    auto draft = [&](std::string value)
    {
        REQUIRE(text->Dispatch({.kind = InputKind::KeyDown, .key = K::A, .control = true}));
        REQUIRE(text->Dispatch({.kind = InputKind::Text, .text = value}));
    };
    draft("#FFF0D2FF");
    REQUIRE(text->Text() == "#FF-F0-D2-FF");
    REQUIRE(text->Finish(EditPhase::Commit));
    REQUIRE(text->Text() == "#FF-F0-D2-FF");
    draft("255,240,210,255");
    REQUIRE(text->Text() == "255,240,210,255");
    REQUIRE(text->Finish(EditPhase::Commit));
    picker.SetValue(LLUtils::Color{uint32_t{0x01020304}});
    REQUIRE(text->Text() == "1,2,3,4");
    draft("0X01020304");
    REQUIRE(text->Finish(EditPhase::Commit));
    REQUIRE(text->Text() == "0X01-02-03-04");
    draft("255,0,0,255");
    REQUIRE(text->Finish(EditPhase::Cancel));
    REQUIRE(picker.Value() == LLUtils::Color{uint32_t{0x01020304}});
    REQUIRE(text->Text() == "0X01-02-03-04");
    REQUIRE(phases.back() == EditPhase::Cancel);
    draft("256,0,0,255");
    REQUIRE_FALSE(text->InputError().empty());
    REQUIRE(text->Finish(EditPhase::Commit));
    REQUIRE(picker.Value() == LLUtils::Color{uint32_t{0x01020304}});
    REQUIRE(text->Finish(EditPhase::Cancel));
    draft("0");
    REQUIRE_FALSE(text->Finish(EditPhase::Commit));
    REQUIRE(text->Dispatch({.kind = InputKind::Text, .text = "x01020304"}));
    REQUIRE(text->Finish(EditPhase::Commit));
    REQUIRE(text->Text() == "0x01-02-03-04");
}

TEST_CASE("Dynamic color masks advance fields and reject edits atomically", "[ui][text][dynamic-mask]")
{
    TextEnvironment env("", {260, 30}, TextBoxMode::SingleLine);
    REQUIRE(env.text->SetInputMask(internal::ColorInputMask()));
    env.Type("#"); env.Type("ff");
    REQUIRE(env.text->Text() == "#FF-");
    env.Type("f0d2ff");
    REQUIRE(env.text->Text() == "#FF-F0-D2-FF");
    REQUIRE(env.text->MaskComplete());
    REQUIRE(env.text->Finish(EditPhase::Commit));
    REQUIRE(env.Key(K::A, true)); env.Type("0"); env.Type("x"); env.Type("01020304");
    REQUIRE(env.text->Text() == "0x01-02-03-04");
    REQUIRE(env.text->MaskComplete());
    REQUIRE(env.text->Finish(EditPhase::Cancel));
    REQUIRE(env.text->Text() == "#FF-F0-D2-FF");
    REQUIRE(env.Key(K::A, true)); env.Type("12"); env.Type(",");
    REQUIRE(env.text->Text() == "12,");
    env.Type("255");
    REQUIRE(env.text->Text() == "12,255,");
    env.Type("256");
    REQUIRE(env.text->Text() == "12,255,");
    REQUIRE_FALSE(env.text->InputError().empty());
    env.Type("2,3");
    REQUIRE(env.text->MaskComplete());
    REQUIRE(env.text->Text() == "12,255,2,3");
    REQUIRE(env.Key(K::Home));
    REQUIRE(env.Key(K::Right));
    REQUIRE(env.Key(K::Delete));
    REQUIRE(env.text->Text() == "1,255,2,3");
    REQUIRE(env.Key(K::Home));
    REQUIRE(env.Key(K::Right));
    REQUIRE(env.Key(K::Right));
    REQUIRE(env.Key(K::Backspace));
    REQUIRE(env.text->Text() == ",255,2,3");
    REQUIRE_FALSE(env.text->MaskComplete());
    REQUIRE_FALSE(env.text->Finish(EditPhase::Commit));
    env.host->SetInvalidEditPolicy(InvalidEditPolicy::DiscardOnLeave);
    REQUIRE(env.host->Focus(env.next));
    REQUIRE(env.text->Text() == "#FF-F0-D2-FF");
    REQUIRE(env.text->Validation().empty());
}
TEST_CASE("Dynamic mask definitions and replacements preserve the previous state on failure", "[ui][text][dynamic-mask]")
{
    TextBox text("#01-02-03-04");
    auto definition = internal::ColorInputMask();
    REQUIRE(text.SetInputMask(definition));
    definition.profiles[0].maximumDigits = 0;
    REQUIRE_FALSE(text.SetInputMask(definition));
    REQUIRE(text.Text() == "#01-02-03-04");
    REQUIRE(text.Dispatch({.kind = InputKind::KeyDown, .key = K::A, .control = true}));
    REQUIRE(text.Dispatch({.kind = InputKind::Text, .text = "256,0,0,0"}));
    REQUIRE(text.Text() == "#01-02-03-04");
    REQUIRE(text.Dispatch({.kind = InputKind::Text, .text = "1,2,3,4"}));
    REQUIRE(text.Text() == "1,2,3,4");
    REQUIRE(text.Finish(EditPhase::Cancel));
    REQUIRE(text.Text() == "#01-02-03-04");
    auto mask = internal::DynamicMask::Create(internal::ColorInputMask());
    REQUIRE(mask);
    auto pasted = mask->Replace("", 0, 0, "0X010203", TextEditOrigin::Paste);
    REQUIRE(pasted);
    REQUIRE(pasted->display == "0X01-02-03-FF");
    REQUIRE_FALSE(mask->Replace("", 0, 0, "#01020z", TextEditOrigin::Paste));
    REQUIRE(mask->Replace("", 0, 0, "#01 02 03 04", TextEditOrigin::Paste)->display == "#01-02-03-04");
}
TEST_CASE("Invalid edit departure policies preserve strict explicit commit", "[ui][text][validation]")
{
    TextEnvironment env("original", {260, 30}, TextBoxMode::SingleLine);
    REQUIRE(env.host->GetInvalidEditPolicy() == InvalidEditPolicy::KeepInvalid);
    REQUIRE(env.Key(K::A, true)); env.Type("invalid"); env.text->SetValidation("Rejected");
    REQUIRE_FALSE(env.host->Finish(EditPhase::Commit));
    env.host->SetInvalidEditPolicy(InvalidEditPolicy::BlockOnLeave);
    REQUIRE_FALSE(env.host->Focus(env.next));
    REQUIRE(env.text->Text() == "invalid");
    env.host->SetInvalidEditPolicy(InvalidEditPolicy::DiscardOnLeave);
    REQUIRE(env.text->Text() == "invalid");
    REQUIRE(env.host->Focus(env.next));
    REQUIRE(env.text->Text() == "original");
    REQUIRE(env.text->Validation().empty());
    REQUIRE(env.host->Focus(env.text));
    env.Type("invalid"); env.text->SetValidation("Rejected");
    REQUIRE(env.Key(K::Tab));
    REQUIRE(env.host->IsFocused(*env.next));
    REQUIRE(env.text->Text() == "original");
}
#ifdef LWS_HAS_WIN32_BACKEND
TEST_CASE("Insert clipboard aliases use existing text paths and honor read-only", "[ui][text][clipboard]")
{
    TextEnvironment env("copy me", {260, 30}, TextBoxMode::SingleLine);
    REQUIRE(env.Key(K::A, true));
    REQUIRE(env.Key(K::Insert, true));
    std::string copied;
    LWS::RequestClipboardText(*env.window, [&](auto status, auto value)
    { if (status == LWS::ClipboardResult::Success) copied = std::move(value); });
    if (copied.empty()) SKIP("Clipboard access unavailable");
    REQUIRE(copied == "copy me");
    REQUIRE(LWS::SetClipboardText(*env.window, "replacement") == LWS::ClipboardResult::Success);
    REQUIRE(env.Key(K::Insert, false, true));
    env.Pump(20);
    REQUIRE(env.text->Text() == "replacement");
    env.text->SetReadOnly(true);
    REQUIRE(env.Key(K::A, true));
    REQUIRE(env.Key(K::Insert, false, true));
    REQUIRE(env.text->Text() == "copy me");
    REQUIRE(env.Key(K::Insert, true));
    LWS::RequestClipboardText(*env.window, [&](auto, auto value) { copied = std::move(value); });
    REQUIRE(copied == "copy me");
}
#endif

TEST_CASE("Color masks reset after deleting all decimal input or all hex input including its prefix", "[ui][text][dynamic-mask]")
{
    TextEnvironment env("#11-22-33-44", {260, 30}, TextBoxMode::SingleLine);
    REQUIRE(env.text->SetInputMask(internal::ColorInputMask()));
    REQUIRE(env.Key(K::End));
    for (unsigned i = 0; i < 8; ++i) REQUIRE(env.Key(K::Backspace));
    REQUIRE(env.text->Text() == "#");
    REQUIRE(env.Key(K::Backspace));
    REQUIRE(env.text->Text().empty());
    env.Type("12,34,56,78");
    REQUIRE(env.text->Text() == "12,34,56,78");
    REQUIRE(env.Key(K::Home)); REQUIRE(env.Key(K::Delete));
    REQUIRE(env.text->Text() == "2,34,56,78");
    REQUIRE(env.Key(K::End));
    for (unsigned i = 0; i < 7; ++i) REQUIRE(env.Key(K::Backspace));
    REQUIRE(env.text->Text().empty());
    env.Type("0"); env.Type("x"); env.Type("11223344");
    REQUIRE(env.text->Text() == "0x11-22-33-44");
    for (unsigned i = 0; i < 8; ++i) REQUIRE(env.Key(K::Backspace));
    REQUIRE(env.text->Text() == "0x");
    REQUIRE(env.Key(K::Backspace));
    REQUIRE(env.text->Text() == "0");
    REQUIRE(env.Key(K::Backspace));
    REQUIRE(env.text->Text().empty());
    env.Type("#11223344");
    REQUIRE(env.Key(K::A, true)); REQUIRE(env.Key(K::Delete));
    REQUIRE(env.text->Text().empty());
    REQUIRE(env.Key(K::Escape));
    REQUIRE(env.text->Text() == "#11-22-33-44");
}
TEST_CASE("Incomplete color drafts preview entered channels without publishing a value", "[ui][color][text]")
{
    ColorPicker picker;
    const LLUtils::Color original{uint32_t{0x11223344}};
    picker.SetValue(original);
    auto* text = dynamic_cast<TextBox*>(picker.Children().front().get());
    auto* swatch = dynamic_cast<ColorSwatch*>(picker.Children().back().get());
    REQUIRE(text); REQUIRE(swatch);
    unsigned edits = 0;
    auto changed = picker.OnEdit.Connect([&](auto, auto) { ++edits; });
    auto draft = [&](std::string value)
    {
        REQUIRE(text->Dispatch({.kind = InputKind::KeyDown, .key = K::A, .control = true}));
        REQUIRE(text->Dispatch({.kind = InputKind::Text, .text = value}));
    };
    draft("#f");
    REQUIRE(swatch->Value() == LLUtils::Color{uint32_t{0x0f0000ff}});
    REQUIRE(picker.Value() == original); REQUIRE(edits == 0);
    draft("12,");
    REQUIRE(swatch->Value() == LLUtils::Color{uint32_t{0x0c0000ff}});
    REQUIRE(picker.Value() == original); REQUIRE(edits == 0);
    draft("12,5");
    REQUIRE(swatch->Value() == LLUtils::Color{uint32_t{0x0c0500ff}});
    REQUIRE_FALSE(text->Finish(EditPhase::Commit));
    REQUIRE(text->Finish(EditPhase::Cancel));
    REQUIRE(swatch->Value() == original);
    REQUIRE(picker.Value() == original);
    REQUIRE(text->Text() == "#11-22-33-44");
}

TEST_CASE("Ambiguous color digits choose hexadecimal when a hex letter is typed", "[ui][text][dynamic-mask]")
{
    TextEnvironment env("", {260, 30}, TextBoxMode::SingleLine);
    REQUIRE(env.text->SetInputMask(internal::ColorInputMask()));
    for (const auto& letter : {"a", "B", "c", "D", "e", "F"})
    {
        REQUIRE(env.Key(K::A, true)); REQUIRE(env.Key(K::Delete));
        env.Type(letter);
        REQUIRE(env.text->Text() == std::string("#") + char(std::toupper(static_cast<unsigned char>(*letter))));
    }
    REQUIRE(env.Key(K::A, true)); REQUIRE(env.Key(K::Delete));
    env.Type("0"); env.Type("f");
    REQUIRE(env.text->Text() == "#0F-");
    env.Type("123456");
    REQUIRE(env.text->Text() == "#0F-12-34-56");
    REQUIRE(env.text->MaskComplete());
    REQUIRE(env.Key(K::A, true)); REQUIRE(env.Key(K::Delete));
    env.Type("12"); env.Type("a"); env.Type("b");
    REQUIRE(env.text->Text() == "#12-AB-");
    REQUIRE(env.Key(K::A, true)); REQUIRE(env.Key(K::Delete));
    env.Type("12"); REQUIRE(env.Key(K::Home)); env.Type("f");
    REQUIRE(env.text->Text() == "#F1-2");
    REQUIRE(env.Key(K::Delete)); env.Type("e");
    REQUIRE(env.text->Text() == "#FE-2");
    REQUIRE(env.Key(K::A, true)); REQUIRE(env.Key(K::Delete));
    env.Type("12,"); env.Type("f");
    REQUIRE(env.text->Text() == "12,");
    REQUIRE_FALSE(env.text->InputError().empty());
    REQUIRE(env.Key(K::A, true)); REQUIRE(env.Key(K::Delete));
    env.Type("123"); env.Type("f");
    REQUIRE(env.text->Text() == "#12-3F-");
    REQUIRE(env.Key(K::A, true)); REQUIRE(env.Key(K::Delete));
    env.Type("g");
    REQUIRE(env.text->Text().empty());
    env.Type("0"); env.Type("x"); env.Type("ab");
    REQUIRE(env.text->Text() == "0xAB-");
}

TEST_CASE("Color paste normalizes bare RGB whitespace and prefix-only fields", "[ui][text][color][clipboard]")
{
    auto mask = internal::DynamicMask::Create(internal::ColorInputMask());
    REQUIRE(mask);
    for (const auto& display : {"", "#", "0x", "0X"})
    {
        auto result = mask->Replace(display, std::string_view(display).size(), std::string_view(display).size(),
                                    "#  123f56", TextEditOrigin::Paste);
        REQUIRE(result);
        REQUIRE(result->display == "#12-3F-56-FF");
        REQUIRE(result->caret == result->display.size());
    }
    for (const auto& paste : {"123f56", "  #  12 3f 56  ", "#12-3f-56", "123F56FF"})
    {
        auto result = mask->Replace("", 0, 0, paste, TextEditOrigin::Paste);
        REQUIRE(result);
        REQUIRE(result->display == "#12-3F-56-FF");
        REQUIRE(mask->Complete(result->display));
    }
    REQUIRE(mask->Replace("0X", 2, 2, "123f56", TextEditOrigin::Paste)->display == "0X12-3F-56-FF");
    REQUIRE(mask->Replace("#", 1, 1, "0x123F56", TextEditOrigin::Paste)->display == "0x12-3F-56-FF");
    REQUIRE(mask->Replace("", 0, 0, "123456", TextEditOrigin::Paste)->display == "#12-34-56-FF");
    for (const auto* invalid : {"#12--3f-56", "#123f5z", "123456789", "12,256,3,4"})
        REQUIRE_FALSE(mask->Replace("#", 1, 1, invalid, TextEditOrigin::Paste));
    REQUIRE(mask->Replace("#12-34-56-FF", 4, 4, "#123F56", TextEditOrigin::Paste)->display == "#12-3F-56-FF");
}
TEST_CASE("Explicit decimal selection and automatic separators have separate draft state", "[ui][text][dynamic-mask]")
{
    TextEnvironment env("", {260, 30}, TextBoxMode::SingleLine);
    REQUIRE(env.text->SetInputMask(internal::ColorInputMask()));
    for (const bool singleEvent : {false, true})
    {
        REQUIRE(env.Key(K::A, true)); REQUIRE(env.Key(K::Delete));
        if (singleEvent) env.Type("123F");
        else { env.Type("1"); env.Type("2"); env.Type("3"); env.Type("F"); }
        REQUIRE(env.text->Text() == "#12-3F-");
        REQUIRE(*internal::PreviewColor(env.text->Text()) == LLUtils::Color{uint32_t{0x123f00ff}});
    }
    REQUIRE(env.Key(K::A, true)); REQUIRE(env.Key(K::Delete));
    env.Type("123"); env.Type(","); env.Type("F");
    REQUIRE(env.text->Text() == "123,");
    REQUIRE_FALSE(env.text->InputError().empty());
    REQUIRE(env.Key(K::A, true)); REQUIRE(env.Key(K::Delete));
    env.Type("123"); env.Type("g");
    REQUIRE(env.text->Text() == "123,");
    env.Type("F");
    REQUIRE(env.text->Text() == "#12-3F-");
    env.text->SetText("1,2,3,4");
    env.Type("f");
    REQUIRE(env.text->Text() == "1,2,3,4");
    REQUIRE(env.Key(K::A, true)); REQUIRE(env.Key(K::Delete));
    env.Type("123F"); REQUIRE(env.Key(K::Escape));
    REQUIRE(env.text->Text() == "1,2,3,4");
    env.Type("f");
    REQUIRE(env.text->Text() == "1,2,3,4");
}
TEST_CASE("Incomplete previews use zero RGB and opaque alpha independently of the old value", "[ui][color][text]")
{
    using internal::PreviewColor;
    for (const auto* value : {"", "#", "0x", "0X", "0"})
        REQUIRE(*PreviewColor(value) == LLUtils::Color{uint32_t{0x000000ff}});
    REQUIRE(*PreviewColor("#f") == LLUtils::Color{uint32_t{0x0f0000ff}});
    REQUIRE(*PreviewColor("#12-3F-") == LLUtils::Color{uint32_t{0x123f00ff}});
    REQUIRE(*PreviewColor("12,5") == LLUtils::Color{uint32_t{0x0c0500ff}});
    REQUIRE(*PreviewColor("12,,5,") == LLUtils::Color{uint32_t{0x0c0005ff}});
    REQUIRE(*PreviewColor("#12-34-56-0") == LLUtils::Color{uint32_t{0x12345600}});
    REQUIRE(*PreviewColor("12,34,56,1") == LLUtils::Color{uint32_t{0x0c223801}});
    REQUIRE_FALSE(PreviewColor("256,"));
}
TEST_CASE("Color spans identify channel digits without coloring prefixes or separators", "[ui][color][text]")
{
    for (const auto* text : {"#12-34-56-ff", "0x12-34-56-ff", "0X12-34-56-ff", "12,34,56,255"})
    {
        const auto spans = internal::ColorTextSpans(text);
        REQUIRE(spans.size() == 4);
        for (size_t i = 0; i < spans.size(); ++i)
        {
            REQUIRE(spans[i].foreground == LLUtils::Color{internal::ColorChannelColors[i]});
            REQUIRE(spans[i].background == LLUtils::Color{uint32_t{0}});
            REQUIRE(std::string_view(text).substr(spans[i].offset, spans[i].length).find_first_not_of("0123456789abcdefABCDEF") == std::string_view::npos);
        }
    }
    auto spans = internal::ColorTextSpans("#12-3F-");
    REQUIRE(spans.size() == 2);
    REQUIRE(static_cast<TextRange>(spans[0]) == TextRange{1, 2});
    REQUIRE(static_cast<TextRange>(spans[1]) == TextRange{4, 2});
    spans = internal::ColorTextSpans("12,,5,");
    REQUIRE(spans.size() == 2);
    REQUIRE(spans[1].foreground == LLUtils::Color{uint32_t{0x3399ffff}});
    REQUIRE(internal::ColorTextSpans("#").empty());
}
TEST_CASE("Text span providers respect selection UTF-8 boundaries and removal", "[ui][text][render]")
{
    for (const auto mode : {TextBoxMode::SingleLine, TextBoxMode::Multiline})
    {
        TextEnvironment env("RGB", {260, 100}, mode);
        env.text->SetText("RGB");
        auto render = [&]
        {
            Canvas canvas; canvas.Begin(400, 300);
            env.text->Render(canvas);
            const auto bitmap = canvas.End();
            return std::vector<std::byte>(bitmap.pixels.begin(), bitmap.pixels.end());
        };
        const auto normal = render();
        unsigned calls = 0;
        env.text->SetTextSpanProvider([&](std::string_view text)
        {
            ++calls; REQUIRE(text == "RGB");
            return std::vector<TextSpan>{{{0, 3}, LLUtils::Color{uint32_t{0xff0000ff}}, LLUtils::Color{uint32_t{0}}}};
        });
        REQUIRE(bool(render() != normal));
        REQUIRE(calls == 1);
        REQUIRE(env.Key(K::A, true));
        const auto selected = render();
        env.text->SetTextSpanProvider({});
        REQUIRE(bool(render() == selected));
        env.text->SetText("RGB");
        REQUIRE(bool(render() == normal));
        env.text->SetText("\xc3\xa9X");
        const auto unicode = render();
        env.text->SetTextSpanProvider([](std::string_view)
        {
            const LLUtils::Color red{uint32_t{0xff0000ff}}, clear{uint32_t{0}};
            return std::vector<TextSpan>{{{1, 1}, red, clear}, {{0, 1}, red, clear},
                                         {{99, 2}, red, clear}, {{0, 99}, red, clear}, {{0, 0}, red, clear}};
        });
        REQUIRE(bool(render() == unicode));
    }
}

#ifdef LWS_HAS_WIN32_BACKEND
TEST_CASE("Color clipboard paste replaces a provisional prefix and completes RGB alpha", "[ui][text][color][clipboard]")
{
    TextEnvironment env("", {260, 30}, TextBoxMode::SingleLine);
    REQUIRE(env.text->SetInputMask(internal::ColorInputMask()));
    for (const auto* initial : {"#", ""})
    {
        env.text->SetText(initial);
        const std::string paste = std::string_view(initial).empty() ? "123f56" : "#  123f56";
        auto status = LWS::SetClipboardText(*env.window, paste);
        for (unsigned attempt = 0; attempt < 20; ++attempt)
        {
            status = LWS::SetClipboardText(*env.window, paste);
            if (status == LWS::ClipboardResult::Success) break;
            env.Pump(5);
        }
        if (status != LWS::ClipboardResult::Success) SKIP("Clipboard access unavailable");
        REQUIRE(env.Key(K::V, true)); env.Pump(20);
        REQUIRE(env.text->Text() == "#12-3F-56-FF");
        REQUIRE(env.text->MaskComplete());
        REQUIRE(*internal::ParseColor(env.text->Text()) == LLUtils::Color{uint32_t{0x123f56ff}});
    }
}
#endif

TEST_CASE("TextBox background override stays black when focused unfocused and borderless", "[ui][text][render]")
{
    TextEnvironment env("color", {260, 40}, TextBoxMode::SingleLine);
    env.text->SetBackground(LLUtils::Color{uint32_t{0x000000ff}});
    for (const bool borderless : {false, true})
    {
        env.text->SetBorderless(borderless);
        for (const bool focused : {false, true})
        {
            REQUIRE(env.host->Focus(focused ? static_cast<Control*>(env.text) : env.next));
            Canvas canvas; canvas.Begin(400, 300);
            env.text->Render(canvas);
            const auto pixels = canvas.End();
            const auto bounds = env.text->Bounds();
            const auto offset = size_t(bounds.y + 2) * pixels.rowPitch + size_t(bounds.x + bounds.width - 4) * 4;
            REQUIRE(std::to_integer<unsigned>(pixels.pixels[offset]) == 0);
            REQUIRE(std::to_integer<unsigned>(pixels.pixels[offset + 1]) == 0);
            REQUIRE(std::to_integer<unsigned>(pixels.pixels[offset + 2]) == 0);
            REQUIRE(std::to_integer<unsigned>(pixels.pixels[offset + 3]) == 255);
        }
    }
}

TEST_CASE("Color fields preserve charcoal fill and show focus below validation across themes", "[ui][text][render][color]")
{
    TextEnvironment env("#12-34-56-78", {260, 40}, TextBoxMode::SingleLine);
    internal::ConfigureColorTextBox(*env.text);
    const auto bounds = env.text->Bounds();
    for (const auto preset : ThemePresets)
    {
        const auto theme = MakeTheme(preset);
        env.text->SetStyle(theme);
        for (const bool readOnly : {false, true})
        {
            env.text->SetReadOnly(readOnly);
            env.text->SetBorderless(readOnly);
            for (const bool focused : {false, true})
            {
                REQUIRE(env.host->Focus(focused ? static_cast<Control*>(env.text) : env.next));
                Canvas canvas; canvas.Begin(400, 300);
                env.text->Render(canvas);
                const auto pixels = canvas.End();
                auto pixel = [&](float x, float y)
                {
                    const auto at = size_t(y) * pixels.rowPitch + size_t(x) * 4;
                    return LLUtils::Color{(uint32_t(std::to_integer<unsigned>(pixels.pixels[at + 2])) << 24) |
                        (uint32_t(std::to_integer<unsigned>(pixels.pixels[at + 1])) << 16) |
                        (uint32_t(std::to_integer<unsigned>(pixels.pixels[at])) << 8) |
                        std::to_integer<unsigned>(pixels.pixels[at + 3])};
                };
                REQUIRE(pixel(bounds.x + bounds.width - 5, bounds.y + 4) == LLUtils::Color{uint32_t{0x161616ff}});
                REQUIRE(pixel(bounds.x + 1, bounds.y + 1) ==
                    (focused ? theme.accent : LLUtils::Color{uint32_t{0x161616ff}}));
                REQUIRE(env.text->Bounds().width == bounds.width);
                REQUIRE(env.text->Bounds().height == bounds.height);
            }
        }
        REQUIRE(env.host->Focus(env.text));
        env.text->SetValidation("Rejected");
        Canvas canvas; canvas.Begin(400, 300); env.text->Render(canvas);
        const auto pixels = canvas.End();
        const auto at = size_t(bounds.y + bounds.height - 1) * pixels.rowPitch + size_t(bounds.x + 20) * 4;
        REQUIRE(std::to_integer<unsigned>(pixels.pixels[at + 2]) == theme.error.R());
        REQUIRE(std::to_integer<unsigned>(pixels.pixels[at + 1]) == theme.error.G());
        REQUIRE(std::to_integer<unsigned>(pixels.pixels[at]) == theme.error.B());
        env.text->SetValidation("");
    }
}

TEST_CASE("Color typography inherits theme fonts while keeping semibold minimum size", "[ui][text][font][color]")
{
    TextEnvironment env("MMMM", {260, 80}, TextBoxMode::SingleLine);
    env.text->fontRole = Control::FontRole::Small;
    auto theme = env.text->Style();
    theme.smallFont.family = "Arial";
    theme.smallFont.size = 12;
    theme.smallFont.weight = 300;
    env.text->SetStyle(theme);
    internal::ConfigureColorTextBox(*env.text);
    const Control& base = *env.text;
    REQUIRE(base.Font().size == 16);
    REQUIRE(base.Font().weight == 600);
    REQUIRE(base.Font().family == "Arial");
    theme.smallFont.size = 26;
    theme.smallFont.weight = 700;
    theme.smallFont.family = "DejaVu Sans";
    env.text->SetStyle(theme);
    REQUIRE(base.Font().size == 26);
    REQUIRE(base.Font().weight == 600);
    REQUIRE(base.Font().family == "DejaVu Sans");
    const auto bounds = env.text->Bounds();
    const float x = bounds.x + theme.textPadding + env.host->MeasureCaret("MMMM", 2, env.text->Font()) + .1f;
    REQUIRE(env.host->Route({.kind = InputKind::Down, .x = x, .y = bounds.y + 14}));
    REQUIRE(env.host->Route({.kind = InputKind::Up, .x = x, .y = bounds.y + 14}));
    env.Type("X");
    REQUIRE(env.text->Text() == "MMXMM");
    REQUIRE(env.text->Finish(EditPhase::Commit));
    env.text->SetMinimumFontSize(40);
    const auto wanted = env.host->MeasureText("Mg", 260, false, env.text->Font()).height + 2 * theme.textPadding;
    REQUIRE(env.text->Measure({260, 500}).height >= wanted);
    env.text->SetFontWeight(std::nullopt);
    env.text->SetMinimumFontSize(0);
    REQUIRE(env.text->Font().weight == 700);
    REQUIRE(env.text->Font().size == 26);
}
TEST_CASE("Multiline typography overrides update wrapping and caret geometry", "[ui][text][font][multiline]")
{
    TextEnvironment env("alpha beta gamma delta epsilon zeta", {170, 130});
    env.text->SetMinimumFontSize(24);
    env.text->SetFontWeight(600);
    REQUIRE(env.Key(K::End, true));
    env.Type("!");
    REQUIRE(env.text->Text().ends_with("zeta!"));
    const auto lines = env.host->MeasureTextLines(env.text->Text(), env.Width(), env.text->Font());
    REQUIRE(lines.size() > 1);
    REQUIRE(env.Key(K::Home, true));
    REQUIRE(env.Key(K::Down));
    env.Type("X");
    const auto inserted = env.text->Text().find('X');
    REQUIRE(inserted != std::string::npos);
    REQUIRE(inserted >= lines[1].range.offset);
}

TEST_CASE("Partial color paste starts editable decimal or hexadecimal drafts", "[ui][text][color][clipboard]")
{
    auto mask = internal::DynamicMask::Create(internal::ColorInputMask());
    REQUIRE(mask);
    const auto decimal = mask->Replace("", 0, 0, "111,111", TextEditOrigin::Paste);
    REQUIRE(decimal);
    REQUIRE(decimal->display == "111,111,");
    REQUIRE_FALSE(mask->Complete(decimal->display));
    REQUIRE(*internal::PreviewColor(decimal->display) == LLUtils::Color{uint32_t{0x6f6f00ff}});
    const auto hex = mask->Replace("", 0, 0, "123F", TextEditOrigin::Paste);
    REQUIRE(hex);
    REQUIRE(hex->display == "#12-3F-");
    REQUIRE_FALSE(mask->Complete(hex->display));
    REQUIRE(*internal::PreviewColor(hex->display) == LLUtils::Color{uint32_t{0x123f00ff}});
    REQUIRE(mask->Replace("", 0, 0, "123", TextEditOrigin::Paste)->display == "123,");
    REQUIRE(mask->Replace("#", 1, 1, "123F", TextEditOrigin::Paste)->display == "#12-3F-");
    REQUIRE(mask->Replace("", 0, 0, "#12-3", TextEditOrigin::Paste)->display == "#12-3");
    REQUIRE(mask->Replace("", 0, 0, "123f56", TextEditOrigin::Paste)->display == "#12-3F-56-FF");
    REQUIRE(mask->Replace("", 0, 0, "111,111,", TextEditOrigin::Paste)->display == "111,111,");
    REQUIRE(mask->Replace("#FF-FF-FF-FF", 5, 5, "111,111", TextEditOrigin::Paste)->display == "111,111,");
    REQUIRE(mask->Replace("1,2,3,4", 2, 3, "123F", TextEditOrigin::Paste)->display == "#12-3F-");
    for (const auto* invalid : {"111,,111", "111,256", "1 11,111", "#12--3F", "123FG", "123456789"})
        REQUIRE_FALSE(mask->Replace("", 0, 0, invalid, TextEditOrigin::Paste));
}

#ifdef LWS_HAS_WIN32_BACKEND
TEST_CASE("Pasting partial colors replaces the current draft and Escape restores it", "[ui][text][color][clipboard]")
{
    TextEnvironment env("#11-22-33-44", {260, 40}, TextBoxMode::SingleLine);
    REQUIRE(env.text->SetInputMask(internal::ColorInputMask()));
    for (const auto* paste : {"111,111", "123F"})
    {
        REQUIRE(env.Key(K::Home)); REQUIRE(env.Key(K::Right));
        auto status = LWS::SetClipboardText(*env.window, paste);
        for (unsigned attempt = 0; status != LWS::ClipboardResult::Success && attempt < 20; ++attempt)
        { env.Pump(5); status = LWS::SetClipboardText(*env.window, paste); }
        if (status != LWS::ClipboardResult::Success) SKIP("Clipboard access unavailable");
        REQUIRE(env.Key(K::V, true)); env.Pump(20);
        REQUIRE(env.text->Text() == (std::string_view(paste).find(',') != std::string_view::npos ? "111,111," : "#12-3F-"));
        REQUIRE_FALSE(env.text->MaskComplete());
        REQUIRE_FALSE(env.text->Finish(EditPhase::Commit));
        REQUIRE(env.Key(K::Escape));
        REQUIRE(env.text->Text() == "#11-22-33-44");
    }
}
#endif

TEST_CASE("Out-of-range undecided decimal input chooses hexadecimal", "[ui][text][color][inference]")
{
    TextEnvironment env("", {260, 40}, TextBoxMode::SingleLine);
    REQUIRE(env.text->SetInputMask(internal::ColorInputMask()));
    env.Type("3"); env.Type("3"); env.Type("3");
    REQUIRE(env.text->Text() == "#33-3");
    REQUIRE_FALSE(env.text->MaskComplete());
    REQUIRE(env.Key(K::A, true)); env.Type("256");
    REQUIRE(env.text->Text() == "#25-6");
    REQUIRE(env.Key(K::A, true)); env.Type("255");
    REQUIRE(env.text->Text() == "255,");
    auto mask = internal::DynamicMask::Create(internal::ColorInputMask());
    REQUIRE(mask->Replace("", 0, 0, "333", TextEditOrigin::Paste)->display == "#33-3");
    REQUIRE_FALSE(mask->Replace("", 0, 0, "1,333", TextEditOrigin::Paste));
    REQUIRE(env.Key(K::A, true)); env.Type("1,"); env.Type("333");
    REQUIRE(env.text->Text() == "1,");
}
TEST_CASE("RGB input is complete with optional alpha and high-nibble trailing hex digits", "[ui][text][color]")
{
    using internal::ParseColor;
    REQUIRE(*ParseColor("#1234F") == LLUtils::Color{uint32_t{0x1234f0ff}});
    REQUIRE(*ParseColor("#12-34-F") == LLUtils::Color{uint32_t{0x1234f0ff}});
    REQUIRE(*ParseColor("#12-34-F-A") == LLUtils::Color{uint32_t{0x1234f0a0}});
    REQUIRE(*ParseColor("0x123456A") == LLUtils::Color{uint32_t{0x123456a0}});
    REQUIRE(*ParseColor("12,34,56") == LLUtils::Color{uint32_t{0x0c2238ff}});
    REQUIRE(*ParseColor("12,34,56,") == LLUtils::Color{uint32_t{0x0c2238ff}});
    REQUIRE_FALSE(ParseColor("#123F"));
    REQUIRE_FALSE(ParseColor("#12-3-F"));
    REQUIRE_FALSE(ParseColor("12,34,"));
    ColorPicker picker;
    const LLUtils::Color original{uint32_t{0x11223344}};
    picker.SetValue(original);
    auto* text = dynamic_cast<TextBox*>(picker.Children().front().get());
    auto draft = [&](const char* value)
    {
        REQUIRE(text->Dispatch({.kind = InputKind::KeyDown, .key = K::A, .control = true}));
        REQUIRE(text->Dispatch({.kind = InputKind::Text, .text = value}));
    };
    draft("1234f");
    REQUIRE(text->Text() == "#12-34-F");
    REQUIRE(text->MaskComplete());
    REQUIRE(picker.Value() == LLUtils::Color{uint32_t{0x1234f0ff}});
    REQUIRE(text->Finish(EditPhase::Cancel));
    REQUIRE(picker.Value() == original);
    draft("1234f"); REQUIRE(text->Finish(EditPhase::Commit));
    REQUIRE(text->Text() == "#12-34-F0-FF");
    draft("123456a"); REQUIRE(text->Finish(EditPhase::Commit));
    REQUIRE(text->Text() == "#12-34-56-A0");
    draft("12,34,56"); REQUIRE(text->Finish(EditPhase::Commit));
    REQUIRE(text->Text() == "12,34,56,255");
    auto mask = internal::DynamicMask::Create(internal::ColorInputMask());
    const auto pasted = mask->Replace("", 0, 0, "1234f", TextEditOrigin::Paste);
    REQUIRE(pasted); REQUIRE(mask->Complete(pasted->display));
    REQUIRE(*ParseColor(pasted->display) == LLUtils::Color{uint32_t{0x1234f0ff}});
    REQUIRE(*ParseColor(mask->Replace("", 0, 0, "#12-34-F-A", TextEditOrigin::Paste)->display) == LLUtils::Color{uint32_t{0x1234f0a0}});
}
TEST_CASE("Byte fields accept decimal and implicit or prefixed hexadecimal", "[ui][text][color]")
{
    TextEnvironment env("0", {260, 40}, TextBoxMode::SingleLine);
    REQUIRE(env.text->SetInputMask(internal::ColorByteInputMask()));
    REQUIRE(env.text->MaskComplete());
    for (const auto* input : {"FF", "#FF", "0xFF", "0Xff", "255"})
    {
        REQUIRE(env.Key(K::A, true)); env.Type(input);
        REQUIRE(env.text->MaskComplete());
        REQUIRE(*internal::ParseColorByte(env.text->Text()) == 255);
        REQUIRE(env.text->Finish(EditPhase::Commit));
    }
    REQUIRE(env.Key(K::A, true)); env.Type("f");
    REQUIRE(env.text->Text() == "#F");
    REQUIRE(*internal::ParseColorByte(env.text->Text()) == 15);
    REQUIRE(env.text->Finish(EditPhase::Cancel));
    REQUIRE(env.text->Text() == "255");
    REQUIRE(env.Key(K::A, true)); env.Type("0x");
    REQUIRE_FALSE(env.text->MaskComplete());
    env.Type("80"); REQUIRE(*internal::ParseColorByte(env.text->Text()) == 128);
    REQUIRE_FALSE(internal::ParseColorByte("333"));
    REQUIRE_FALSE(internal::ParseColorByte("0x100"));
}

TEST_CASE("Double-click actions consume one native double press and preserve draft cancellation", "[ui][text][double-click]")
{
    TextEnvironment env("original", {260, 40}, TextBoxMode::SingleLine);
    unsigned toggles = 0, previews = 0, commits = 0;
    auto edited = env.text->OnEdit.Connect([&](const auto&, auto phase)
    { previews += phase == EditPhase::Preview; commits += phase == EditPhase::Commit; });
    env.text->SetDoubleClickAction([&] { ++toggles; return env.text->EditText("converted"); });
    const auto bounds = env.text->Bounds();
    auto pointer = [&](bool pressed, unsigned count)
    {
        auto input = internal::InputFromEvent(LWS::EventMouseButton{LWS::MouseButton::Left, pressed,
            {int32_t(bounds.x + 10), int32_t(bounds.y + 10)}, count}, env.context);
        REQUIRE(input); REQUIRE(input->clickCount == count); REQUIRE(env.host->Route(*input));
    };
    pointer(true, 1); pointer(false, 1);
    REQUIRE(toggles == 0);
    pointer(true, 2); pointer(false, 1);
    REQUIRE(toggles == 1); REQUIRE(previews == 1); REQUIRE(commits == 0);
    REQUIRE(env.host->IsFocused(*env.text));
    REQUIRE(env.text->Text() == "converted");
    env.Type("X"); REQUIRE(env.text->Text() == "convertedX");
    REQUIRE(env.Key(K::Escape)); REQUIRE(env.text->Text() == "original");
    pointer(true, 3); pointer(false, 1);
    REQUIRE(toggles == 1);
    env.text->SetDoubleClickAction({});
    pointer(true, 2); pointer(false, 1);
    REQUIRE(toggles == 1);
    env.text->SetReadOnly(true);
    REQUIRE_FALSE(env.text->EditText("forbidden"));
    REQUIRE(env.text->Text() == "original");
}
TEST_CASE("Inline format toggles preserve prefix history numeric values and partial edit restoration", "[ui][text][color][double-click]")
{
    ColorPicker picker;
    const LLUtils::Color original{uint32_t{0x12345678}};
    picker.SetValue(original);
    auto* text = dynamic_cast<TextBox*>(picker.Children().front().get());
    auto toggle = [&]
    {
        REQUIRE(text->Dispatch({.kind = InputKind::Down, .clickCount = 2}));
        REQUIRE(text->Dispatch({.kind = InputKind::Up}));
    };
    REQUIRE(text->EditText("0x12345678")); REQUIRE(text->Finish(EditPhase::Commit));
    toggle(); REQUIRE(text->Text() == "18,52,86,120"); REQUIRE(picker.Value() == original);
    toggle(); REQUIRE(text->Text() == "0x12-34-56-78"); REQUIRE(picker.Value() == original);
    REQUIRE(text->Finish(EditPhase::Cancel)); REQUIRE(text->Text() == "0x12-34-56-78");
    REQUIRE(text->EditText("#12"));
    toggle(); REQUIRE(text->Text() == "18,0,0,255");
    REQUIRE(picker.Value() == LLUtils::Color{uint32_t{0x120000ff}});
    REQUIRE(text->Finish(EditPhase::Cancel));
    REQUIRE(picker.Value() == original); REQUIRE(text->Text() == "0x12-34-56-78");
}
TEST_CASE("Popup formats toggle independently including read-only Original and byte defaults", "[ui][text][color][double-click]")
{
    const LLUtils::Color original{uint32_t{0x11223344}};
    auto preview = original;
    unsigned notifications = 0;
    TextEnvironment env("", {260, 40}, TextBoxMode::SingleLine);
    auto panel = std::make_unique<internal::ColorPanel>(original,
        [&](auto value, bool complete) { if (complete) preview = value; ++notifications; });
    auto* root = panel.get();
    std::vector<TextBox*> fields;
    for (auto& child : root->Children()) if (auto* field = dynamic_cast<TextBox*>(child.get())) fields.push_back(field);
    REQUIRE(fields.size() == 9);
    env.host->SetRoot(std::move(panel));
    root->Measure({720, 680}); root->Arrange({0, 0, 720, 680});
    auto toggle = [&](size_t index)
    {
        const auto b = fields[index]->Bounds();
        REQUIRE(fields[index]->Dispatch({.kind = InputKind::Down, .x = b.x + 5, .y = b.y + 5, .clickCount = 2}));
        REQUIRE(fields[index]->Dispatch({.kind = InputKind::Up, .x = b.x + 5, .y = b.y + 5}));
    };
    toggle(7); REQUIRE(fields[7]->Text() == "17,34,51,68");
    REQUIRE(fields[8]->Text() == "#11-22-33-44"); REQUIRE(fields[0]->Text() == "17");
    const auto beforeOriginal = notifications;
    toggle(8); REQUIRE(fields[8]->Text() == "17,34,51,68"); REQUIRE(notifications == beforeOriginal);
    REQUIRE_FALSE(fields[8]->EditText("forbidden"));
    REQUIRE(fields[7]->Finish(EditPhase::Commit));
    toggle(0); REQUIRE(fields[0]->Text() == "#11");
    REQUIRE(fields[1]->Text() == "34"); REQUIRE(fields[7]->Text() == "17,34,51,68");
    REQUIRE(fields[0]->EditText("0x80")); REQUIRE(fields[0]->Finish(EditPhase::Commit));
    toggle(0); REQUIRE(fields[0]->Text() == "128");
    toggle(0); REQUIRE(fields[0]->Text() == "0x80");
    REQUIRE(fields[1]->EditText("64"));
    REQUIRE(fields[0]->Text() == "0x80"); REQUIRE(fields[8]->Text() == "17,34,51,68");
    REQUIRE(preview == LLUtils::Color{uint32_t{0x80403344}});
    const auto hsl = fields[4]->Text(); const auto beforeHsl = preview;
    toggle(4); REQUIRE(fields[4]->Text() == hsl); REQUIRE(preview == beforeHsl);
    for (size_t channel = 0; channel < 4; ++channel)
    {
        REQUIRE(fields[channel]->EditText("")); toggle(channel);
        REQUIRE(*internal::ParseColorByte(fields[channel]->Text()) == (channel == 3 ? 255 : 0));
    }
    toggle(8); REQUIRE(fields[8]->Text() == "#11-22-33-44");
}

TEST_CASE("Pending clipboard input cannot overwrite a double-click conversion", "[ui][text][double-click][clipboard]")
{
    TextEnvironment env("original", {260, 40}, TextBoxMode::SingleLine);
    auto status = LWS::SetClipboardText(*env.window, "late paste");
    for (unsigned attempt = 0; status != LWS::ClipboardResult::Success && attempt < 20; ++attempt)
    { env.Pump(5); status = LWS::SetClipboardText(*env.window, "late paste"); }
    if (status != LWS::ClipboardResult::Success) SKIP("Clipboard publication unavailable");
    env.text->SetDoubleClickAction([&] { return env.text->EditText("converted"); });
    REQUIRE(env.Key(K::V, true));
    const auto b = env.text->Bounds();
    REQUIRE(env.host->Route({.kind = InputKind::Down, .x = b.x + 10, .y = b.y + 10, .clickCount = 2}));
    REQUIRE(env.host->Route({.kind = InputKind::Up, .x = b.x + 10, .y = b.y + 10}));
    env.Pump(30);
    REQUIRE(env.text->Text() == "converted");
    REQUIRE(env.Key(K::Escape)); REQUIRE(env.text->Text() == "original");
}
TEST_CASE("Per-field validation modes override the host and retain drafts by default", "[ui][text][validation]")
{
    TextEnvironment env("25", {260, 40}, TextBoxMode::SingleLine);
    REQUIRE(env.text->EditText("999"));
    env.text->SetValidation("Use 1 to 100");
    REQUIRE(env.host->Focus(env.next));
    REQUIRE(env.text->Text() == "999");
    REQUIRE_FALSE(env.host->Finish(EditPhase::Commit));
    REQUIRE(env.host->Focus(env.text));
    env.text->SetInvalidEditPolicy(InvalidEditPolicy::BlockOnLeave);
    REQUIRE_FALSE(env.host->Focus(env.next));
    env.text->SetInvalidEditPolicy(InvalidEditPolicy::DiscardOnLeave);
    REQUIRE(env.host->Focus(env.next));
    REQUIRE(env.text->Text() == "25");
    REQUIRE(env.text->ValidationError().empty());
}
TEST_CASE("Rejected filtered edits preserve selection and do not invalidate a valid draft", "[ui][text][filter][validation]")
{
    TextEnvironment env("25", {260, 40}, TextBoxMode::SingleLine);
    env.text->SetInputFilter(InputFilters::Number(true));
    REQUIRE(env.Key(K::A, true));
    env.Type("x");
    REQUIRE(env.text->Text() == "25");
    REQUIRE(env.text->InputError() == "Use integer digits");
    REQUIRE(env.text->ValidationError().empty());
    env.Type("42");
    REQUIRE(env.text->Text() == "42");
    REQUIRE(env.text->InputError().empty());
    REQUIRE(env.host->Finish(EditPhase::Commit));
}
TEST_CASE("Blocked focus departure explains the error and clears feedback after editing", "[ui][text][validation]")
{
    TextEnvironment env("25", {260, 40}, TextBoxMode::SingleLine);
    env.text->SetInvalidEditPolicy(InvalidEditPolicy::BlockOnLeave);
    auto validation = env.text->OnEdit.Connect([&](const auto& value, EditPhase phase)
    {
        if (phase == EditPhase::Preview && value == "999") env.text->SetValidation("Use 1 to 100");
    });
    REQUIRE(env.text->EditText("999"));
    REQUIRE(env.text->InputFeedback().empty());
    REQUIRE(env.Key(K::Tab));
    REQUIRE(env.host->IsFocused(*env.text));
    REQUIRE(env.text->InputFeedback().find("Cannot leave this field") != std::string::npos);
    REQUIRE(env.text->InputFeedback().find("Use 1 to 100") != std::string::npos);
    REQUIRE(env.text->InputFeedback().find("Escape") != std::string::npos);
    const auto b = env.next->Bounds();
    REQUIRE(env.host->Route({.kind = InputKind::Down, .x = b.x + 5, .y = b.y + 5}));
    REQUIRE(env.host->IsFocused(*env.text));
    REQUIRE(env.text->Text() == "999");
    REQUIRE(env.text->EditText("99"));
    REQUIRE(env.text->InputFeedback().empty());
    REQUIRE(env.host->Focus(env.next));
    REQUIRE(env.host->Focus(env.text));
    REQUIRE(env.text->EditText("999"));
    REQUIRE_FALSE(env.host->Focus(env.next));
    REQUIRE(env.Key(K::Escape));
    REQUIRE(env.text->InputFeedback().empty());
    REQUIRE(env.text->Text() == "99");
}
TEST_CASE("Whole-value paste handles provisional prefixes without a fallback profile", "[ui][text][dynamic-mask]")
{
    DynamicMaskDefinition definition;
    definition.pasteReplacesValue = true;
    definition.profiles.push_back({"0x", 16, 1, 1, 2, 0, 255, '-'});
    auto mask = internal::DynamicMask::Create(definition);
    REQUIRE(mask);
    auto pasted = mask->Replace("0", 1, 1, "0xAB", TextEditOrigin::Paste);
    REQUIRE(pasted); REQUIRE(pasted->display == "0xAB");
    REQUIRE_FALSE(mask->Replace("0", 1, 1, "AB", TextEditOrigin::Paste));
    definition.normalizePaste = [](std::string_view value, std::string_view prefix)
        -> std::expected<std::string, std::string>
    { REQUIRE(prefix.empty()); return "0x" + std::string(value); };
    mask = internal::DynamicMask::Create(definition);
    pasted = mask->Replace("0", 1, 1, "AB", TextEditOrigin::Paste);
    REQUIRE(pasted); REQUIRE(pasted->display == "0xAB");
}
TEST_CASE("Static-mask validation agrees with optional empty and incomplete commit", "[ui][text][validation][mask]")
{
    TextBox text;
    REQUIRE(text.SetInputMask("00:00"));
    REQUIRE(text.ValidationError().empty());
    REQUIRE(UIHost::CollectValidationIssues(text).empty());
    REQUIRE(text.Finish(EditPhase::Commit));
    REQUIRE(text.EditText("12"));
    REQUIRE_FALSE(text.ValidationError().empty());
    REQUIRE_FALSE(text.Finish(EditPhase::Commit));
    REQUIRE(text.Finish(EditPhase::Cancel));
    REQUIRE(text.ValidationError().empty());
    REQUIRE(text.EditText(""));
    text.SetValidation("Required");
    REQUIRE_FALSE(text.ValidationError().empty());
    REQUIRE_FALSE(text.Finish(EditPhase::Commit));
}
TEST_CASE("Popup validation inherits its logical owner's policy", "[ui][text][validation][popup]")
{
    TextEnvironment env("25", {260, 40}, TextBoxMode::SingleLine);
    env.text->SetInvalidEditPolicy(InvalidEditPolicy::BlockOnLeave);
    auto popup = std::make_unique<StackPanel>();
    auto& field = popup->Emplace<TextBox>("valid");
    REQUIRE(env.host->OpenPopup(*env.text, std::move(popup), {20, 80, 240, 80}));
    REQUIRE(env.host->Focus(&field));
    REQUIRE(field.EditText("invalid")); field.SetValidation("Invalid popup draft");
    REQUIRE(env.host->DeparturePolicy(field) == InvalidEditPolicy::BlockOnLeave);
    auto issues = UIHost::CollectValidationIssues(*env.text);
    REQUIRE(issues.size() == 1); REQUIRE(issues[0].control.Get() == &field);
    REQUIRE(issues[0].policy == InvalidEditPolicy::BlockOnLeave);
    REQUIRE_FALSE(env.host->Focus(env.next));
    field.SetInvalidEditPolicy(InvalidEditPolicy::KeepInvalid);
    REQUIRE(env.host->Focus(env.next));
    issues = UIHost::CollectValidationIssues(*env.text);
    REQUIRE(issues.size() == 1);
    REQUIRE(issues[0].policy == InvalidEditPolicy::KeepInvalid);
    env.host->ClosePopup(EditPhase::Cancel);
    REQUIRE(UIHost::CollectValidationIssues(*env.text).empty());
}
TEST_CASE("Outside popup discard honors an owner override rather than the host default", "[ui][text][validation][popup]")
{
    TextEnvironment env("25", {260, 40}, TextBoxMode::SingleLine);
    env.host->SetInvalidEditPolicy(InvalidEditPolicy::BlockOnLeave);
    env.text->SetInvalidEditPolicy(InvalidEditPolicy::DiscardOnLeave);
    auto popup = std::make_unique<StackPanel>();
    auto& field = popup->Emplace<TextBox>("valid");
    REQUIRE(env.host->OpenPopup(*env.text, std::move(popup), {20, 100, 240, 80}, {}, true));
    REQUIRE(env.host->Focus(&field));
    REQUIRE(field.EditText("invalid")); field.SetValidation("Rejected");
    const auto b = env.next->Bounds();
    REQUIRE(env.host->Route({.kind = InputKind::Down, .x = b.x + b.width / 2, .y = b.y + b.height / 2}));
    REQUIRE_FALSE(env.host->HasPopup());
    REQUIRE(env.host->IsFocused(*env.next));
    REQUIRE(env.text->Text() == "25");
}


TEST_CASE("Dynamic mask completion resolves decimal zero before an unfinished hex prefix", "[ui][text][dynamic-mask][regression]")
{
    DynamicMaskDefinition definition;
    definition.profiles = {{"0x", 16, 1, 1, 2, 0, 255, '-'}, {"", 10, 1, 1, 3, 0, 255, ','}};
    TextBox text;
    REQUIRE(text.SetInputMask(definition));
    for (const auto value : {"0", "25", "255", "0x00", "0xFF"})
    {
        REQUIRE(text.EditText(value));
        REQUIRE(text.MaskComplete()); REQUIRE(text.Finish(EditPhase::Commit));
    }
    REQUIRE(text.EditText("0x")); REQUIRE_FALSE(text.MaskComplete());
    REQUIRE_FALSE(text.Finish(EditPhase::Commit));
}




TEST_CASE("Informational dialogs preserve invalid edits and deliver dismissal immediately", "[ui][text][dialog]")
{
    TextEnvironment env("start", {260, 40}, TextBoxMode::SingleLine);
    env.host->SetInvalidEditPolicy(InvalidEditPolicy::BlockOnLeave);
    REQUIRE(env.text->EditText("invalid")); env.text->SetValidation("Rejected");
    for (const auto key : {K::Enter, K::Escape})
    {
        unsigned dismissed = 0;
        env.host->ShowMessageBox(*env.host->Root(), "Information", "Correct the draft", [&]
        {
            ++dismissed;
            REQUIRE(env.text->Text() == "invalid");
            REQUIRE(env.host->Focus(env.next));
        });
        REQUIRE(env.host->HasPopup());
        REQUIRE(env.text->Text() == "invalid");
        REQUIRE(env.Key(key));
        REQUIRE_FALSE(env.host->HasPopup()); REQUIRE(dismissed == 1);
        REQUIRE(env.host->IsFocused(*env.next));
        REQUIRE(env.text->Text() == "invalid");
    }
}

TEST_CASE("Decision dialogs restore a popup before delivering a choice", "[ui][text][dialog][popup]")
{
    TextEnvironment env("start", {260, 40}, TextBoxMode::SingleLine);
    REQUIRE(env.window->SetVisible(true) == LWS::Result::Success);
    for (unsigned i = 0; i < 500 && !env.window->IsConfigured(); ++i) env.Pump(2);
    REQUIRE(env.window->IsConfigured());
    env.Pump(20);
    auto popup = std::make_unique<StackPanel>();
    auto& field = popup->Emplace<TextBox>("valid");
    REQUIRE(env.host->OpenPopup(*env.text, std::move(popup), {20, 100, 240, 80}, {}, true));
    REQUIRE(env.host->Focus(&field));
    REQUIRE(field.EditText("invalid")); field.SetValidation("Rejected");
    unsigned selected = 0, cancelled = 0;
    const auto open = [&]
    {
        REQUIRE(env.host->ShowDecisionBox(*env.host->Root(), "Decision", "Choose an action",
            {{"Continue", [&]
                {
                    ++selected;
                    REQUIRE(env.host->HasPopupWithin(*env.text));
                    REQUIRE(env.host->IsFocused(field));
                    REQUIRE(field.Text() == "invalid");
                }}}, "Keep editing", [&] { ++cancelled; }));
    };
    open();
    REQUIRE(env.Key(K::Escape)); env.host->Update();
    REQUIRE(cancelled == 1); REQUIRE(selected == 0);
    REQUIRE(env.host->HasPopupWithin(*env.text)); REQUIRE(env.host->IsFocused(field));
    REQUIRE(field.Text() == "invalid");
    env.host->ShowMessageBox(*env.host->Root(), "Information", "Cannot replace invalid popup");
    REQUIRE(env.host->IsFocused(field)); REQUIRE(field.Text() == "invalid");
    REQUIRE_FALSE(field.InputFeedback().empty());
    open();
    REQUIRE(env.Key(K::Tab, false, true));
    REQUIRE(env.Key(K::Enter));
    REQUIRE(selected == 0);
    env.host->Update();
    REQUIRE(selected == 1); REQUIRE(cancelled == 1);
    REQUIRE(field.Text() == "invalid");
}

TEST_CASE("Draft-preserving actions obey mouse buttons capture and disabled ancestry", "[ui][text][input][regression]")
{
    TextEnvironment env("start", {260, 40}, TextBoxMode::SingleLine);
    env.next->preserveEditOnFocus = env.next->allowDuringPopup = true;
    unsigned clicks = 0;
    auto clicked = env.next->OnClick.Connect([&] { ++clicks; });
    const auto b = env.next->Bounds();
    Input input{.kind = InputKind::Down, .x = b.x + 5, .y = b.y + 5, .button = LWS::MouseButton::Right};
    env.host->Route(input); input.kind = InputKind::Up; env.host->Route(input);
    REQUIRE(clicks == 0); env.host->CloseContextMenu();
    const auto text = env.text->Bounds();
    REQUIRE(env.host->Route({.kind = InputKind::Down, .x = text.x + 5, .y = text.y + 5}));
    env.host->Route({.kind = InputKind::Up, .x = b.x + 5, .y = b.y + 5});
    REQUIRE(clicks == 0);
    env.host->Root()->SetEnabled(false);
    env.host->Route({.kind = InputKind::Down, .x = b.x + 5, .y = b.y + 5});
    env.host->Route({.kind = InputKind::Up, .x = b.x + 5, .y = b.y + 5});
    REQUIRE(clicks == 0);
    env.host->Root()->SetEnabled(true);
    REQUIRE(env.host->Route({.kind = InputKind::Down, .x = b.x + 5, .y = b.y + 5}));
    REQUIRE(env.host->Route({.kind = InputKind::Up, .x = b.x + 5, .y = b.y + 5}));
    REQUIRE(clicks == 1);
}
#endif
