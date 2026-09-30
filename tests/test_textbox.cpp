#ifdef LWS_TEST_UI
    #include <catch2/catch_test_macros.hpp>
    #include <catch2/catch_approx.hpp>
    #include <LWSUI/Composites.hpp>
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
        explicit TextEnvironment(std::string value, Size size = {260, 100})
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
            text = &stack->Emplace<TextBox>(std::move(value), TextBoxMode::Multiline);
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
#endif
