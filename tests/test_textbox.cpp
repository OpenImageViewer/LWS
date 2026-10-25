#ifdef LWS_TEST_UI
    #include <catch2/catch_test_macros.hpp>
    #include <catch2/catch_approx.hpp>
    #include <LWSUI/Containers.hpp>
    #include <LWSUI/UIHost.hpp>
    #include <LWS/TextClipboard.hpp>
    #include <LWS/Platform.hpp>
    #ifdef LWS_HAS_WIN32_BACKEND
        #include <LWS/Win32/Platform.hpp>
    #endif
    #include <chrono>
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
