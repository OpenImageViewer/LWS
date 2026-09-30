#ifdef LWS_TEST_SHOWCASE
    #include <catch2/catch_test_macros.hpp>
    #include <LWSUI/Composites.hpp>
    #include "demo/Showcase.hpp"
    #include "demo/SplitPreview.hpp"
    #include "source/MenuHost.hpp"
    #include <catch2/catch_approx.hpp>
    #include <array>
    #include <set>
    #include <thread>
    #ifdef LWS_HAS_WIN32_BACKEND
        #include <LWS/Win32/Platform.hpp>
        #include <LWS/Win32/WindowExtensions.hpp>
    #endif

TEST_CASE("Showcase tiles flexible panes without resizing the fixed sample", "[ui][showcase][layout]")
{
    using LWSUI::demo::ArrangeWorkspace;
    const auto first = ArrangeWorkspace({0, 100, 1100, 600}, .4f);
    const auto wide = ArrangeWorkspace({0, 100, 1400, 800}, .4f);
    const auto dragged = ArrangeWorkspace({0, 100, 1100, 600}, .7f);
    for (const auto& layout : {first, wide, dragged})
    {
        REQUIRE(layout.showFixed);
        REQUIRE(layout.fixed.width == 240);
        REQUIRE(layout.fixed.height == 160);
        REQUIRE(layout.gallery.y + layout.gallery.height < layout.fixed.y);
        REQUIRE(layout.gallery.x + layout.gallery.width <= layout.divider.x);
        REQUIRE(layout.divider.x + layout.divider.width <= layout.preview.x);
        REQUIRE(layout.fixed.x + layout.fixed.width <= layout.divider.x);
    }
    REQUIRE(wide.gallery.width > first.gallery.width);
    REQUIRE(wide.preview.width > first.preview.width);
    REQUIRE(wide.gallery.height > first.gallery.height);
    REQUIRE(dragged.gallery.width > first.gallery.width);
    REQUIRE(dragged.preview.width < first.preview.width);
    const auto tiny = ArrangeWorkspace({0, 0, 180, 150}, .4f);
    REQUIRE_FALSE(tiny.showFixed);
    REQUIRE(tiny.gallery.width >= 0);
    REQUIRE(tiny.preview.width >= 0);
}

TEST_CASE("Showcase catalog binds every public control to the connected demo", "[ui][showcase]")
{
    #ifdef LWS_HAS_WIN32_BACKEND
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    #endif
    LWS::PlatformContext platform;
    #ifdef LWS_HAS_WIN32_BACKEND
    REQUIRE(platform.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    #else
    REQUIRE(platform.Init({.backend = LWS::BackendId::Wayland}) == LWS::Result::Success);
    #endif
    LWS::Window window(platform);
    REQUIRE(window.Create({.clientSize = {560, 620}, .visible = true, .clientSizeLimits = {{560, 620}, {560, 620}}}) ==
            LWS::Result::Success);
    LWSUI::UIHost menuHost(window);
    LWSUI::demo::Showcase showcase(platform, menuHost);
    const auto pump = [&]
    {
        for (int i = 0; i < 25; ++i)
        {
            REQUIRE(platform.ProcessMessages() == LWS::LoopResult::Continue);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    pump();
    REQUIRE(showcase.Verify());
    REQUIRE(showcase.MainWindow().GetWindowStyles() == LWS::WindowStyle::ResizableBorder);
    #ifdef LWS_HAS_WIN32_BACKEND
    REQUIRE((GetWindowLongW(*LWS::Win32::GetHwnd(showcase.MainWindow()), GWL_STYLE) & WS_CAPTION) == 0);
    #endif
    // This explicit inventory is checked against the public control headers, including
    // standalone controls that are also hidden inside composite widgets.
    const std::set<std::string_view> expected{"Control",
                                              "Container",
                                              "Label",
                                              "Button",
                                              "CheckBox",
                                              "RadioButton",
                                              "TextBox",
                                              "Slider",
                                              "ScrollBar",
                                              "ColorSwatch",
                                              "RadioGroup",
                                              "ComboBox",
                                              "NumericEdit<int64_t>",
                                              "NumericEdit<double>",
                                              "ColorPicker",
                                              "FilePicker",
                                              "StackPanel",
                                              "Grid",
                                              "ScrollView",
                                              "TreeView",
                                              "TreeView::Branch",
                                              "TreeView::Row",
                                              "MenuBar"};
    std::set<std::string_view> actual;
    const auto find = [&](std::string_view type)
    {
        for (auto sample : showcase.Samples())
            if (sample.type == type)
                return sample.control;
        return static_cast<LWSUI::Control*>(nullptr);
    };
    for (auto sample : showcase.Samples())
    {
        REQUIRE(sample.control != nullptr);
        REQUIRE(sample.control->Host() != nullptr);
        REQUIRE(actual.insert(sample.type).second);
    }
    REQUIRE(actual == expected);
    for (const auto& sample : showcase.Samples())
        REQUIRE(sample.control->Host()->Style().background == LWSUI::MakeTheme(LWSUI::ThemePreset::Light).background);
    auto& split = showcase.PreviewContainers();
    REQUIRE(split.Pane(0).Content() == find("Control"));
    for (size_t i = 0; i < 5; ++i)
        REQUIRE(split.Pane(i).Host() == find("Control")->Host());
    const auto nativeSize = split.Host()->Window().GetClientAreaMetrics().logical;
    const auto divider = split.Divider(0).Bounds();
    REQUIRE(split.Host()->Route({.kind = LWSUI::InputKind::Down, .x = divider.x + 3, .y = divider.y + 3}));
    REQUIRE(split.Dragging());
    REQUIRE(split.Host()->Route({.kind = LWSUI::InputKind::Move, .x = divider.x + 3, .y = divider.y - 8}));
    split.Host()->Update();
    REQUIRE(split.Host()->Route({.kind = LWSUI::InputKind::Up, .x = divider.x + 3, .y = divider.y - 8}));
    REQUIRE(split.Host()->Window().GetClientAreaMetrics().logical == nativeSize);
    split.ResetSplit();
    split.Host()->Update();
    auto* quick = dynamic_cast<LWSUI::StackPanel*>(split.Pane(2).Content());
    REQUIRE(quick != nullptr);
    auto* quickSlider = dynamic_cast<LWSUI::Slider*>(quick->Children()[2].get());
    REQUIRE(quickSlider != nullptr);
    const auto quickBounds = quickSlider->Bounds();
    REQUIRE(quickSlider->Dispatch(
        {.kind = LWSUI::InputKind::Down, .x = quickBounds.x + quickBounds.width / 2, .y = quickBounds.y + 2}));
    REQUIRE(dynamic_cast<LWSUI::Slider*>(find("Slider"))->Value() == Catch::Approx(.5));
    REQUIRE(quickSlider->Dispatch({.kind = LWSUI::InputKind::Cancel}));
    REQUIRE(dynamic_cast<LWSUI::Slider*>(find("Slider"))->Value() == Catch::Approx(.85));
    showcase.SetTheme(LWSUI::ThemePreset::Dark);
    showcase.Command("Reset layout");
    REQUIRE(split.Host()->Style().background == LWSUI::MakeTheme(LWSUI::ThemePreset::Dark).background);
    auto* numeric = dynamic_cast<LWSUI::NumericEdit<int64_t>*>(find("NumericEdit<int64_t>"));
    auto* slider = dynamic_cast<LWSUI::Slider*>(find("Slider"));
    auto* scene = dynamic_cast<LWSUI::ComboBox*>(find("ComboBox"));
    REQUIRE(numeric != nullptr);
    REQUIRE(slider != nullptr);
    REQUIRE(scene != nullptr);
    REQUIRE(numeric->Dispatch({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Up}));
    REQUIRE(numeric->Dispatch({.kind = LWSUI::InputKind::KeyUp, .key = LWS::KeyCode::Up}));
    REQUIRE(numeric->Value() == 6);
    showcase.Command("Tiles");
    REQUIRE(scene->Value() == "Tiles");
    REQUIRE(dynamic_cast<LWSUI::Label*>(split.Pane(3).Content())->Text().find("Tiles") != std::string::npos);
    REQUIRE(dynamic_cast<LWSUI::Label*>(split.Pane(4).Content())->Text().find("Tiles") != std::string::npos);
    REQUIRE(dynamic_cast<LWSUI::RadioButton*>(find("RadioButton"))->Value() == false);
    showcase.SetTheme(LWSUI::ThemePreset::Light);
    REQUIRE(find("Control")->Host()->Style().background == LWSUI::MakeTheme(LWSUI::ThemePreset::Light).background);
    auto* title = dynamic_cast<LWSUI::TextBox*>(find("TextBox"));
    REQUIRE(title != nullptr);
    REQUIRE(title->Host()->Focus(title));
    REQUIRE(title->Dispatch({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::A, .control = true}));
    REQUIRE(title->Dispatch({.kind = LWSUI::InputKind::Text, .text = std::string(81, 'x')}));
    REQUIRE_FALSE(title->Validation().empty());
    showcase.Reset();
    REQUIRE(title->Validation().empty());
    REQUIRE(title->Text() == "A connected showcase");
    REQUIRE(numeric->Value() == 5);
    REQUIRE(slider->Value() == .85);
    REQUIRE(scene->Value() == "Circles");
    auto& controls = find("TextBox")->Host()->Window();
    #ifdef LWS_HAS_WIN32_BACKEND
    SendMessageW(*LWS::Win32::GetHwnd(controls), WM_CLOSE, 0, 0);
    pump();
    REQUIRE(controls.IsCreated());
    REQUIRE_FALSE(controls.IsVisible());
    #else
    REQUIRE(controls.SetVisible(false) == LWS::Result::Success);
    #endif
    showcase.Command("Controls Gallery");
    REQUIRE(controls.IsVisible());
    REQUIRE(showcase.MainWindow().RequestPlacement({.clientSize = LWS::LogicalSize{800, 580}}) == LWS::Result::Success);
    pump();
    find("Container")->Host()->Update();
    REQUIRE(showcase.Verify());
    auto& host = *find("Container")->Host();
    auto& child = find("StackPanel")->Host()->Window();
    auto* workspace = dynamic_cast<LWSUI::Container*>(host.Root());
    REQUIRE(workspace != nullptr);
    auto* toolbar = dynamic_cast<LWSUI::Container*>(workspace->Children().front().get());
    REQUIRE(toolbar != nullptr);
    REQUIRE(host.Focus(toolbar->Children().front().get()));
    const auto beforeToolbarKey = child.GetPlacement().clientSize;
    REQUIRE_FALSE(host.Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Left}));
    host.Update();
    REQUIRE(child.GetPlacement().clientSize == beforeToolbarKey);
    const auto placement = child.GetPlacement();
    REQUIRE(placement.position.has_value());
    const float x = float(placement.position->x + placement.clientSize.x + 5), y = float(placement.position->y + 20);
    REQUIRE(host.Route({.kind = LWSUI::InputKind::Down, .x = x, .y = y}));
    REQUIRE(host.Route({.kind = LWSUI::InputKind::Move, .x = x + 60, .y = y}));
    host.Update();
    REQUIRE(child.GetPlacement().clientSize.x > placement.clientSize.x);
    REQUIRE(host.Route({.kind = LWSUI::InputKind::Cancel}));
    #ifdef LWS_HAS_WIN32_BACKEND
    REQUIRE(GetCapture() == nullptr);
    #endif
    REQUIRE(showcase.Verify());
    REQUIRE(window.GetClientAreaMetrics().logical == LWS::LogicalSize{560, 620});
    #ifdef LWS_HAS_WIN32_BACKEND
    SendMessageW(*LWS::Win32::GetHwnd(child), WM_KEYDOWN, VK_F10, 0);
    REQUIRE(host.HasOpenMenu());
    SendMessageW(*LWS::Win32::GetHwnd(child), WM_KEYDOWN, VK_DOWN, 0);
    REQUIRE(host.HasOpenMenu());
    SendMessageW(*LWS::Win32::GetHwnd(child), WM_KEYDOWN, VK_ESCAPE, 0);
    REQUIRE_FALSE(host.HasOpenMenu());
    const auto close = LWSUI::internal::MenuSessionAccess::WindowButtonBounds(*host.MainMenu(),
                                                                              LWSUI::internal::MenuWindowAction::Close);
    REQUIRE(host.Route({.kind = LWSUI::InputKind::Down, .x = close.x + 2, .y = close.y + 2}));
    REQUIRE(host.Route({.kind = LWSUI::InputKind::Up, .x = close.x + 2, .y = close.y + 2}));
    REQUIRE(platform.ProcessMessages() == LWS::LoopResult::Quit);
    #endif
}
TEST_CASE("Internal split containers preserve drafts and native geometry", "[ui][showcase][split]")
{
    #ifdef LWS_HAS_WIN32_BACKEND
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    #endif
    LWS::PlatformContext platform;
    #ifdef LWS_HAS_WIN32_BACKEND
    REQUIRE(platform.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    #else
    REQUIRE(platform.Init({.backend = LWS::BackendId::Wayland}) == LWS::Result::Success);
    #endif
    LWS::Window window(platform);
    REQUIRE(window.Create({.clientSize = {1000, 850}, .visible = true}) == LWS::Result::Success);
    LWSUI::UIHost host(window, LWSUI::MakeTheme(LWSUI::ThemePreset::Light));
    auto text = std::make_unique<LWSUI::TextBox>("committed");
    auto* editor = text.get();
    auto details = std::make_unique<LWSUI::Label>(std::string(2000, 'a'));
    details->wrap = true;
    auto splitOwner = std::make_unique<LWSUI::demo::SplitPreview>(std::array<std::unique_ptr<LWSUI::Control>, 5>{
        std::move(text), std::move(details), std::make_unique<LWSUI::Button>("Action"),
        std::make_unique<LWSUI::Label>("Inspector"), std::make_unique<LWSUI::Label>("Activity")});
    auto& split = *splitOwner;
    host.SetRoot(std::move(splitOwner));
    for (int n = 0; n < 25; ++n)
    {
        REQUIRE(platform.ProcessMessages() == LWS::LoopResult::Continue);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    host.Update();
    const auto size = window.GetClientAreaMetrics().logical;
    #ifdef LWS_HAS_WIN32_BACKEND
    const auto hwnd = *LWS::Win32::GetHwnd(window);
    REQUIRE(GetWindow(hwnd, GW_CHILD) == nullptr);
    #endif
    int commits = 0;
    auto edits = editor->OnEdit.Connect(
        [&](const std::string&, LWSUI::EditPhase phase)
        {
            if (phase == LWSUI::EditPhase::Commit)
                ++commits;
        });
    REQUIRE(host.Focus(editor));
    REQUIRE(host.Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::A, .control = true}));
    REQUIRE(host.Route({.kind = LWSUI::InputKind::Text, .text = "draft"}));
    split.Pane(1).SetOffset(30);
    host.Update();
    const auto drag = [&](size_t index, float delta, bool cancel)
    {
        const auto grip = split.Divider(index).Bounds();
        const float x = grip.x + grip.width / 2, y = grip.y + grip.height / 2;
        const auto before = split.Pane(2 - index * 2).Parent()->Bounds();
        const auto ratios = split.Proportions();
        REQUIRE(host.Route({.kind = LWSUI::InputKind::Down, .x = x, .y = y}));
        REQUIRE(host.Route({.kind = LWSUI::InputKind::Move,
                            .x = x + (split.IsSideBySide() ? delta : 0),
                            .y = y + (split.IsSideBySide() ? 0 : delta)}));
        host.Update();
        const auto after = split.Pane(2 - index * 2).Parent()->Bounds();
        REQUIRE(after.width == Catch::Approx(before.width));
        REQUIRE(after.height == Catch::Approx(before.height));
        REQUIRE(split.Proportions() != ratios);
        if (cancel)
            REQUIRE(host.Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Escape}));
        else
            REQUIRE(host.Route({.kind = LWSUI::InputKind::Up, .x = x, .y = y}));
        host.Update();
        REQUIRE_FALSE(split.Dragging());
        if (cancel)
            REQUIRE(split.Proportions() == ratios);
        REQUIRE(host.IsFocused(*editor));
        REQUIRE(editor->Text() == "draft");
        REQUIRE(commits == 0);
        REQUIRE(split.Pane(1).Offset() == 30);
        REQUIRE(window.GetClientAreaMetrics().logical == size);
    #ifdef LWS_HAS_WIN32_BACKEND
        REQUIRE(*LWS::Win32::GetHwnd(window) == hwnd);
        REQUIRE(GetWindow(hwnd, GW_CHILD) == nullptr);
        REQUIRE(GetCapture() == nullptr);
    #endif
    };
    const auto checkLayout = [&]
    {
        const auto normalized = [&](size_t index)
        {
            const auto b = split.Pane(index).Parent()->Bounds();
            return split.IsSideBySide() ? b : LWSUI::Rect{b.y, b.x, b.height, b.width};
        };
        const auto drawing = normalized(0), details = normalized(1), quick = normalized(2);
        const auto inspector = normalized(3), activity = normalized(4);
        REQUIRE(inspector.x == Catch::Approx(drawing.x));
        REQUIRE(inspector.width == Catch::Approx(drawing.width + 8 + details.width));
        REQUIRE(drawing.y == Catch::Approx(details.y));
        REQUIRE(drawing.height == Catch::Approx(details.height));
        REQUIRE(inspector.y == Catch::Approx(drawing.y + drawing.height + 8));
        REQUIRE(activity.x == Catch::Approx(quick.x));
        REQUIRE(activity.width == Catch::Approx(quick.width));
        REQUIRE(activity.y == Catch::Approx(quick.y + quick.height + 8));
    };
    const auto dragCross = [&](size_t index, bool cancel)
    {
        const auto divider = split.Divider(index).Bounds();
        const float x = divider.x + divider.width / 2, y = divider.y + divider.height / 2;
        const auto saved = split.CrossProportions();
        const auto frozenIndex = index == 2 ? 2 : 0;
        const auto frozen = split.Pane(frozenIndex).Parent()->Bounds();
        REQUIRE(host.Route({.kind = LWSUI::InputKind::Down, .x = x, .y = y}));
        REQUIRE(host.Route({.kind = LWSUI::InputKind::Move,
                            .x = x + (split.IsSideBySide() ? 0 : 12),
                            .y = y + (split.IsSideBySide() ? 12 : 0)}));
        host.Update();
        REQUIRE(split.CrossProportions() != saved);
        const auto unchanged = split.Pane(frozenIndex).Parent()->Bounds();
        REQUIRE(unchanged.width == Catch::Approx(frozen.width));
        REQUIRE(unchanged.height == Catch::Approx(frozen.height));
        checkLayout();
        if (cancel)
            REQUIRE(host.Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Escape}));
        else
            REQUIRE(host.Route({.kind = LWSUI::InputKind::Up, .x = x, .y = y}));
        host.Update();
        if (cancel)
            REQUIRE(split.CrossProportions() == saved);
        REQUIRE_FALSE(split.Dragging());
        REQUIRE(host.IsFocused(*editor));
        REQUIRE(editor->Text() == "draft");
        REQUIRE(commits == 0);
        REQUIRE(window.GetClientAreaMetrics().logical == size);
    #ifdef LWS_HAS_WIN32_BACKEND
        REQUIRE(*LWS::Win32::GetHwnd(window) == hwnd);
        REQUIRE(GetWindow(hwnd, GW_CHILD) == nullptr);
        REQUIRE(GetCapture() == nullptr);
    #endif
    };
    for (const bool side : {false, true})
    {
        split.SetSideBySide(side);
        split.ResetSplit();
        host.Update();
        REQUIRE(split.IsSideBySide() == side);
        drag(0, 12, false);
        drag(1, -12, true);
        checkLayout();
        dragCross(2, false);
        dragCross(3, true);
    }
    const auto grip = split.Divider(0).Bounds();
    const auto saved = split.Proportions();
    REQUIRE(host.Route({.kind = LWSUI::InputKind::Down, .x = grip.x + 2, .y = grip.y + 2}));
    REQUIRE(host.Route({.kind = LWSUI::InputKind::Move, .x = grip.x + 22, .y = grip.y + 2}));
    host.Update();
    // A resize that forces the fallback cancels the drag before changing axes.
    split.Arrange({0, 0, 400, 600});
    REQUIRE_FALSE(split.Dragging());
    REQUIRE_FALSE(split.IsSideBySide());
    REQUIRE(split.PrefersSideBySide());
    REQUIRE(split.Proportions() == saved);
    split.Arrange({0, 0, 1000, 850});
    REQUIRE(split.IsSideBySide());
    REQUIRE(host.Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Escape}));
    REQUIRE(editor->Text() == "committed");
    REQUIRE(host.Focus(&split.Divider(0)));
    REQUIRE(host.Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Right}));
    REQUIRE(host.Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Home}));
    REQUIRE(split.Proportions() == std::array<float, 3>{.5f, .3f, .2f});
    REQUIRE(split.CrossProportions() == std::array<float, 2>{.65f, .65f});
    host.Update();
    REQUIRE(host.Focus(&split.Divider(2)));
    REQUIRE(host.Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Down}));
    REQUIRE(split.CrossProportions()[0] > .65f);
    REQUIRE(host.Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Home}));
    REQUIRE(split.CrossProportions() == std::array<float, 2>{.65f, .65f});
    host.Update();
    const auto initial = split.Proportions();
    const auto begin = [&]
    {
        const auto divider = split.Divider(0).Bounds();
        REQUIRE(host.Route({.kind = LWSUI::InputKind::Down, .x = divider.x + 2, .y = divider.y + 2}));
        REQUIRE(host.Route({.kind = LWSUI::InputKind::Move, .x = divider.x + 100000, .y = divider.y + 2}));
        host.Update();
        REQUIRE(split.Pane(1).Parent()->Bounds().width >= 139.99f);
    };
    begin();
    #ifdef LWS_HAS_WIN32_BACKEND
    SendMessageW(hwnd, WM_CAPTURECHANGED, 0, 0);
    #else
    REQUIRE(host.Route({.kind = LWSUI::InputKind::Cancel}));
    #endif
    REQUIRE_FALSE(split.Dragging());
    REQUIRE(split.Proportions() == initial);
    host.Update();
    begin();
    split.SetSideBySide(false);
    REQUIRE_FALSE(split.Dragging());
    REQUIRE(split.Proportions() == initial);
    host.Update();
    REQUIRE_FALSE(split.IsSideBySide());
    const auto compactBounds = LWSUI::Rect{0, 0, 220, 240};
    split.Arrange(compactBounds);
    for (size_t i = 0; i < 5; ++i)
    {
        const auto bounds = split.Pane(i).Parent()->Bounds();
        REQUIRE(bounds.height >= 0);
        REQUIRE(bounds.y + bounds.height <= compactBounds.height + .01f);
    }
}
#endif
