#ifdef LWS_TEST_UI
#include <LWSUI/Composites.hpp>
#include <thread>
#include <chrono>
    #include <catch2/catch_test_macros.hpp>
    #include <LWSUI/Containers.hpp>
    #include <LWSUI/UIHost.hpp>
    #include <LWS/Platform.hpp>
    #ifdef LWS_HAS_WIN32_BACKEND
        #include <LWS/Win32/Platform.hpp>
    #endif
    #include <memory>
    #include <string>
    #include <vector>

TEST_CASE("UI event dispatch respects connection changes", "[ui][event]")
{
    LWSUI::Event<void()> event;
    std::vector<int> calls;
    LWSUI::Event<void()>::Connection first, second, added;
    first = event.Connect(
        [&]
        {
            calls.push_back(1);
            // Removing the running callback and its successor must not invalidate dispatch.
            first.Disconnect();
            second.Disconnect();
            added = event.Connect([&] { calls.push_back(3); });
        });
    second = event.Connect([&] { calls.push_back(2); });
    event.Raise();
    REQUIRE(calls == std::vector<int>{1});
    event.Raise();
    REQUIRE(calls == std::vector<int>{1, 3});
}

TEST_CASE("UI event connections may outlive their publisher", "[ui][event][lifetime]")
{
    auto event = std::make_unique<LWSUI::Event<void()>>();
    unsigned calls = 0;
    auto destroy = event->Connect(
        [&]
        {
            ++calls;
            event.reset();
        });
    auto later = event->Connect([&] { ++calls; });
    event->Raise();
    REQUIRE_FALSE(event);
    REQUIRE(calls == 1);
    destroy.Disconnect();
    later.Disconnect();
}

TEST_CASE("Text edits preserve UTF-8 and transactional validation", "[ui][text]")
{
    LWSUI::TextBox text("base");
    REQUIRE(text.Dispatch({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::End}));
    std::vector<LWSUI::EditPhase> phases;
    auto edited = text.OnEdit.Connect([&](const std::string&, LWSUI::EditPhase phase) { phases.push_back(phase); });
    REQUIRE(text.Dispatch({.kind = LWSUI::InputKind::Text, .text = "\xc3\xa9\xf0\x9f\x98\x80"}));
    REQUIRE(text.Dispatch({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Backspace}));
    REQUIRE(text.Text() == "base\xc3\xa9");
    text.SetValidation("Rejected draft");
    REQUIRE_FALSE(text.Finish(LWSUI::EditPhase::Commit));
    REQUIRE(text.Finish(LWSUI::EditPhase::Cancel));
    REQUIRE(text.Text() == "base");
    REQUIRE(text.Validation().empty());
    REQUIRE(phases.back() == LWSUI::EditPhase::Cancel);
    REQUIRE(text.Dispatch({.kind = LWSUI::InputKind::Text, .text = "saved\n"}));
    REQUIRE(text.Finish(LWSUI::EditPhase::Commit));
    const auto committed = text.Text();
    REQUIRE(committed.find('\n') == std::string::npos);
    REQUIRE(text.Dispatch({.kind = LWSUI::InputKind::Text, .text = "draft"}));
    text.SetReadOnly(true);
    REQUIRE(text.Text() == committed);
    REQUIRE(text.Dispatch({.kind = LWSUI::InputKind::Text, .text = "ignored"}));
    REQUIRE(text.Text() == committed);
}

TEST_CASE("UI popups preserve rejected drafts and cancel with Escape", "[ui][popup]")
{
    #ifdef LWS_HAS_WIN32_BACKEND
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    #endif
    LWS::PlatformContext context;
    #ifdef LWS_HAS_WIN32_BACKEND
    REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    #else
    REQUIRE(context.Init({.backend = LWS::BackendId::Wayland}) == LWS::Result::Success);
    #endif
    LWS::Window window(context);
    REQUIRE(window.Create({.visible = true}) == LWS::Result::Success);
    LWSUI::UIHost host(window);
    auto root = std::make_unique<LWSUI::Button>("Owner");
    auto* owner = root.get();
    host.SetRoot(std::move(root));
    auto popup = std::make_unique<LWSUI::TextBox>("original");
    auto* editor = popup.get();
    LWSUI::EditPhase closed = LWSUI::EditPhase::Preview;
    REQUIRE(host.OpenPopup(
        *owner, std::move(popup), {0, 0, 200, 100}, [&](LWSUI::EditPhase phase) { closed = phase; }, true));
    REQUIRE(host.Focus(editor));
    REQUIRE(host.Route({.kind = LWSUI::InputKind::Text, .text = "draft"}));
    editor->SetValidation("Rejected draft");
    REQUIRE_FALSE(host.OpenPopup(*owner, std::make_unique<LWSUI::Button>("Replacement"), {0, 0, 200, 100}));
    REQUIRE(host.HasPopup());
    REQUIRE(host.IsFocused(*editor));
    REQUIRE(host.Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Escape}));
    REQUIRE_FALSE(host.HasPopup());
    REQUIRE(closed == LWSUI::EditPhase::Cancel);
    REQUIRE(host.IsFocused(*owner));
}

TEST_CASE("UI context menus defer commands and preserve the text draft", "[ui][menu]")
{
    #ifdef LWS_HAS_WIN32_BACKEND
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    #endif
    LWS::PlatformContext context;
    #ifdef LWS_HAS_WIN32_BACKEND
    REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    #else
    REQUIRE(context.Init({.backend = LWS::BackendId::Wayland}) == LWS::Result::Success);
    #endif
    LWS::Window window(context);
    REQUIRE(window.Create({.visible = true}) == LWS::Result::Success);
    LWSUI::UIHost host(window);
    auto root = std::make_unique<LWSUI::TextBox>("original");
    auto* editor = root.get();
    host.SetRoot(std::move(root));
    REQUIRE(host.Focus(editor));
    REQUIRE(host.Route({.kind = LWSUI::InputKind::Text, .text = "draft"}));
    const auto draft = editor->Text();
    unsigned calls = 0;
    editor->SetContextMenuProvider(
        [&](LWSUI::Control&, const LWSUI::ContextMenuRequest&)
        { return std::vector<LWSUI::ContextMenuItem>{{"Run", "", [&](LWSUI::Control&) { ++calls; }}}; });
    REQUIRE(host.Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::F10, .shift = true}));
    REQUIRE(host.HasContextMenu());
    REQUIRE(host.IsFocused(*editor));
    REQUIRE(host.Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Enter}));
    REQUIRE_FALSE(host.HasContextMenu());
    REQUIRE(calls == 0);
    host.Update();
    REQUIRE(calls == 1);
    REQUIRE(editor->Text() == draft);
    REQUIRE(editor->Finish(LWSUI::EditPhase::Cancel));
    REQUIRE(editor->Text() == "original");
}

TEST_CASE("Scrolling preserves child edits and clips hit testing", "[ui][scroll]")
{
    auto content = std::make_unique<LWSUI::StackPanel>();
    auto& editor = content->Emplace<LWSUI::TextBox>("original");
    for (int index = 0; index < 20; ++index)
        content->Emplace<LWSUI::Button>("Row");
    LWSUI::ScrollView scroll;
    scroll.SetContent(std::move(content));
    scroll.Measure({300, 100});
    scroll.Arrange({0, 0, 300, 100});
    REQUIRE(editor.Dispatch({.kind = LWSUI::InputKind::Text, .text = "draft"}));
    const auto draft = editor.Text();
    REQUIRE(scroll.Dispatch({.kind = LWSUI::InputKind::Wheel, .wheel = -1}));
    REQUIRE(scroll.Offset() > 0);
    scroll.Arrange({0, 0, 300, 100});
    REQUIRE(scroll.HitTest(2, -1) == nullptr);
    REQUIRE(editor.Text() == draft);
    scroll.SetOffset(0);
    scroll.Arrange({0, 0, 300, 100});
    REQUIRE(scroll.HitTest(2, 2) == &editor);
    REQUIRE(editor.Finish(LWSUI::EditPhase::Cancel));
    REQUIRE(editor.Text() == "original");
}



TEST_CASE("Content-sized multiline text uses its enclosing viewport", "[ui][textbox][scrollview]")
{
    using namespace LWSUI;
    #ifdef LWS_HAS_WIN32_BACKEND
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    #endif
    LWS::PlatformContext platform;
    #ifdef LWS_HAS_WIN32_BACKEND
    REQUIRE(platform.Init({LWS::BackendId::Win32}) == LWS::Result::Success);
    #else
    REQUIRE(platform.Init({LWS::BackendId::Wayland}) == LWS::Result::Success);
    #endif
    LWS::Window window(platform);
    REQUIRE(window.Create({.clientSize = {400, 200}, .visible = true}) == LWS::Result::Success);
    for (int i = 0; i < 500 && !window.IsConfigured(); ++i)
    {
        REQUIRE(platform.ProcessMessages() == LWS::LoopResult::Continue);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(window.IsConfigured());
    UIHost host(window);
    auto root = std::make_unique<ScrollView>();
    auto& scroll = *root;
    std::string content;
    for (int i = 0; i < 100; ++i) content += "Wrapped selectable content. ";
    auto ownedText = std::make_unique<TextBox>(content, TextBoxMode::Multiline);
    auto& text = *ownedText;
    text.sizeToContent = true;
    text.SetReadOnly(true);
    scroll.SetContent(std::move(ownedText));
    host.SetRoot(std::move(root));
    host.Update();
    REQUIRE(text.Bounds().height > scroll.Viewport().height);
    REQUIRE(host.Focus(&text));
    REQUIRE(host.Route({.kind = InputKind::KeyDown, .key = LWS::KeyCode::End, .control = true}));
    REQUIRE(scroll.Offset() > 0);
    REQUIRE(host.Route({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Home, .control = true}));
    REQUIRE(scroll.Offset() == 0);
    const auto visible = scroll.Viewport();
    const float x = visible.x + 20, y = visible.y + 12;
    REQUIRE(host.Route({.kind = InputKind::Down, .x = x, .y = y}));
    REQUIRE(host.Route({.kind = InputKind::Move, .x = x, .y = visible.y + visible.height + 25}));
    const float initial = scroll.Offset();
    REQUIRE(initial > 0);
    for (int i = 0; i < 10; ++i)
    {
        REQUIRE(platform.ProcessMessages() == LWS::LoopResult::Continue);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(scroll.Offset() > initial);
    host.Route({.kind = InputKind::Up, .x = x, .y = y});
    scroll.SetOffset(0);
    REQUIRE(host.Route({.kind = InputKind::Wheel, .x = x, .y = y, .wheel = -1}));
    REQUIRE(scroll.Offset() > 0);
    const float beforeLeave = scroll.Offset();
    int leaves = 0;
    auto leave = scroll.OnLeave.Connect([&] { ++leaves; });
    host.Route({.kind = InputKind::Move, .x = x, .y = y});
    host.Route({.kind = InputKind::Move, .x = -10, .y = -10});
    REQUIRE(leaves == 1);
    REQUIRE(platform.ProcessMessages() == LWS::LoopResult::Continue);
    REQUIRE(scroll.Offset() == beforeLeave);
    for (auto& child : text.Children()) REQUIRE_FALSE(child->Visible());
    text.SetReadOnly(false);
    host.Route({.kind = InputKind::KeyDown, .key = LWS::KeyCode::A, .control = true});
    REQUIRE(host.Route({.kind = InputKind::Text, .text = content + content}));
    host.Update();
    REQUIRE(scroll.Offset() > beforeLeave);
    const float editedOffset = scroll.Offset();
    const auto bottom = scroll.Viewport();
    REQUIRE(host.Route({.kind = InputKind::Wheel, .x = x, .y = bottom.y + 12, .wheel = 1}));
    REQUIRE(scroll.Offset() < editedOffset);
    REQUIRE(text.Finish(EditPhase::Cancel));
    host.Update();
    REQUIRE(text.Text() == content);
}


TEST_CASE("Slider value presentation preserves normalized and exact typed editing", "[ui][slider]")
{
    using namespace LWSUI;
    Slider slider;
    slider.SetValue(.25);
    REQUIRE(slider.ValueDisplay() == SliderValueMode::Hidden);
    REQUIRE(slider.ValueField() == nullptr);
    slider.SetValueDisplay(SliderValueMode::ReadOnly);
    REQUIRE(dynamic_cast<Label*>(slider.ValueField()) != nullptr);
    REQUIRE(slider.Value() == .25);
    slider.SetValueDisplay(SliderValueMode::Editable);
    REQUIRE(dynamic_cast<NumericEdit<double>*>(slider.ValueField()) != nullptr);
    slider.Measure({300, 40});
    slider.Arrange({0, 0, 300, 40});
    REQUIRE(slider.TrackBounds().width < slider.Bounds().width);
    REQUIRE(slider.TrackBounds().x + slider.TrackBounds().width <= slider.ValueField()->Bounds().x);
    slider.SetValueDisplay(SliderValueMode::Hidden);
    REQUIRE(slider.ValueField() == nullptr);
    REQUIRE(slider.TrackBounds().width == 300);
    auto& value = slider.ConfigureValue<int64_t>({9007199254740987LL, 9007199254740991LL, 1}, 9007199254740990LL);
    REQUIRE(slider.Dispatch({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Right}));
    REQUIRE(value.Value() == 9007199254740991LL);
    REQUIRE(slider.Dispatch({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Left}));
    REQUIRE(value.Value() == 9007199254740990LL);
    REQUIRE_THROWS_AS(slider.ConfigureValue<int64_t>({0, 0, 1}, 0), std::invalid_argument);
    REQUIRE(value.Value() == 9007199254740990LL);
    REQUIRE_THROWS_AS(slider.SetValue(std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
    auto* number = dynamic_cast<NumericEdit<int64_t>*>(slider.ValueField());
    REQUIRE(number);
    value.SetValue(9007199254740987LL);
    REQUIRE(number->Value() == value.Value());
    auto& decimal = slider.ConfigureValue<double>({.5, 3., .1}, 1.03);
    slider.Measure({300, 40});
    slider.Arrange({0, 0, 300, 40});
    REQUIRE(slider.Dispatch({.kind = InputKind::Down, .x = 0, .y = 2}));
    REQUIRE(decimal.Value() == .5);
    REQUIRE(slider.Finish(EditPhase::Cancel));
    REQUIRE(decimal.Value() == 1.03);
    REQUIRE(slider.Dispatch({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Right}));
    REQUIRE(decimal.Value() == 1.13);
    std::vector<EditPhase> phases;
    auto edits = decimal.OnEdit.Connect([&](double, EditPhase phase) { phases.push_back(phase); });
    REQUIRE(slider.Dispatch({.kind = InputKind::Down, .x = 0, .y = 2}));
    REQUIRE(slider.Dispatch({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Right}));
    REQUIRE(decimal.Value() == .6);
    REQUIRE(phases == std::vector<EditPhase>{EditPhase::Preview, EditPhase::Commit, EditPhase::Commit});
    REQUIRE(slider.Finish(EditPhase::Cancel));
    REQUIRE(slider.Dispatch({.kind = InputKind::Up}));
    REQUIRE(decimal.Value() == .6);
    REQUIRE(phases.size() == 3);
    ScrollBar scroll;
    scroll.SetValue(.4);
    scroll.Measure({20, 200});
    scroll.Arrange({0, 0, 20, 200});
    REQUIRE(scroll.Value() == .4);
    REQUIRE(scroll.ValueField() == nullptr);
}
#endif
