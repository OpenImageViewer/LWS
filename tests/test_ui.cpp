#ifdef LWS_TEST_UI
    #include <catch2/catch_test_macros.hpp>
    #include <LWSUI/Composites.hpp>
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

#endif
