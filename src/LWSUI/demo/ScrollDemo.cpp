#include <LWS/Platform.hpp>
#include <LWS/Timer.hpp>
#include <LWSUI/Composites.hpp>
#include <LWSUI/UIHost.hpp>
#ifdef LWS_HAS_WIN32_BACKEND
#include <LWS/Win32/Platform.hpp>
#endif
#include <fstream>
#include <sstream>
#include <iostream>
#include "ScrollFeedback.hpp"
using namespace LWSUI;
namespace
{
    constexpr size_t CardSide = 64;
    class Cards final : public Container
    {
      protected:
        Size OnMeasure(Size) override
        {
            ++generation_;
            for (const auto& child : Children()) child->Measure({250, 110});
            arrangedGeneration_.resize(Children().size());
            return {float(side_) * 260, float(side_) * 120};
        }
        void OnArrange() override
        {
            visible_.clear();
            if (!Parent()) return;
            const auto b = Bounds(), viewport = Parent()->Bounds();
            const auto firstColumn = size_t(std::clamp(std::floor((viewport.x - b.x) / 260), 0.f, float(side_)));
            const auto lastColumn = size_t(std::clamp(std::ceil((viewport.x + viewport.width - b.x) / 260), 0.f, float(side_)));
            const auto firstRow = size_t(std::clamp(std::floor((viewport.y - b.y) / 120), 0.f, float(side_)));
            const auto lastRow = size_t(std::clamp(std::ceil((viewport.y + viewport.height - b.y) / 120), 0.f, float(side_)));
            for (size_t row = firstRow; row < lastRow; ++row)
                for (size_t column = firstColumn; column < lastColumn; ++column)
                {
                    const auto index = row * side_ + column;
                    if (index < Children().size())
                    {
                        visible_.push_back(index);
                        ArrangeCard(index);
                    }
                }
            UpdateFocusedCard();
        }
        // These cards contain primitives without preview handlers.
        // Normal dispatch still routes pointer and keyboard input to their controls.
        bool OnPreviewInput(const Input&) override { return false; }
        void OnRender(Canvas& canvas) override
        {
            UpdateFocusedCard();
            for (const auto index : visible_) Children()[index]->Render(canvas);
        }
      public:
        explicit Cards(size_t side) : side_(side) {}
        Control* HitTest(float x, float y) override
        {
            if (!Control::HitTest(x, y)) return nullptr;
            const auto b = Bounds();
            const auto column = size_t((x - b.x) / 260), row = size_t((y - b.y) / 120);
            const auto index = row * side_ + column;
            if (column < side_ && row < side_ && index < Children().size())
            {
                lastHit_ = index;
                ArrangeCard(index);
                if (auto* hit = Children()[index]->HitTest(x, y)) return hit;
            }
            return hitTestBackground ? this : nullptr;
        }
      private:
        void ArrangeCard(size_t index)
        {
            if (arrangedGeneration_.size() != Children().size()) arrangedGeneration_.resize(Children().size());
            const auto b = Bounds(), previous = Children()[index]->Bounds();
            const Rect target{b.x + float(index % side_) * 260, b.y + float(index / side_) * 120, 250, 110};
            if (arrangedGeneration_[index] != generation_ || previous.x != target.x || previous.y != target.y ||
                previous.width != target.width || previous.height != target.height)
            {
                Children()[index]->Arrange(target);
                arrangedGeneration_[index] = generation_;
            }
        }
        void UpdateFocusedCard()
        {
            if (!Host() || !Host()->HasFocusWithin(*this)) { focused_.reset(); return; }
            if (!focused_ || !Host()->HasFocusWithin(*Children()[*focused_]))
            {
                focused_.reset();
                if (lastHit_ && Host()->HasFocusWithin(*Children()[*lastHit_])) focused_ = lastHit_;
                if (!focused_)
                    for (const auto index : visible_)
                        if (Host()->HasFocusWithin(*Children()[index])) { focused_ = index; break; }
                // Keyboard focus can move to a card outside the viewport.
                if (!focused_)
                    for (size_t index = 0; index < Children().size(); ++index)
                        if (Host()->HasFocusWithin(*Children()[index])) { focused_ = index; break; }
            }
            if (focused_) ArrangeCard(*focused_);
        }
        size_t side_;
        uint64_t generation_ = 0;
        std::vector<uint64_t> arrangedGeneration_;
        std::vector<size_t> visible_;
        std::optional<size_t> focused_, lastHit_;
    };
    class Progress final : public Control
    {
      public:
        double speed = 100, maximum = 100;
      protected:
        Size OnMeasure(Size) override { return {100, 20}; }
        Size OnMeasureContent(Size size) override { return OnMeasure(size); }
        void OnRender(Canvas& canvas) override
        {
            const auto b = Bounds();
            canvas.Fill(b.x, b.y, b.width, b.height, Style().surface);
            canvas.Fill(b.x, b.y, b.width * float(std::clamp(speed / maximum, 0., 1.)), b.height, Style().accent);
            if (maximum > 100)
            {
                const float marker = b.x + b.width * float(100 / maximum);
                canvas.Fill(marker, b.y, 1, b.height, Style().foreground);
                const float labelWidth = std::min(48.f, b.width);
                canvas.Text("100%", std::clamp(marker + 3, b.x, b.x + b.width - labelWidth), b.y,
                            labelWidth, b.height, Style().foreground, Style().smallFont);
            }
        }
    };
    void Snapshot(UIHost& host, const std::string& path)
    {
        host.Update();
        const auto size = host.Window().GetClientAreaMetrics().logical;
        Canvas canvas;
        canvas.Begin(size.x, size.y);
        canvas.Fill(0, 0, float(size.x), float(size.y), host.Style().background);
        host.Root()->Render(canvas);
        const auto pixels = canvas.End();
        std::ofstream out(path, std::ios::binary);
        out << "P6\n" << size.x << ' ' << size.y << "\n255\n";
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x)
            {
                const auto* p = pixels.pixels.data() + y * pixels.rowPitch + x * 4;
                const char rgb[]{char(p[2]), char(p[1]), char(p[0])};
                out.write(rgb, 3);
            }
        if (!out) throw std::runtime_error("Cannot write ScrollView snapshot");
    }
}
int main(int argc, char** argv)
{
    try
    {
        bool smoke = false, snapshot = false, narrow = false;
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--smoke") smoke = true;
            else if (arg == "--snapshot") smoke = snapshot = true;
            else if (arg == "--snapshot-narrow") smoke = snapshot = narrow = true;
            else throw std::invalid_argument("Usage: LWSUIScrollDemo [--smoke] [--snapshot] [--snapshot-narrow]");
        }
        const auto check = [](bool result, const char* message) { if (!result) throw std::runtime_error(message); };
#ifdef LWS_HAS_WIN32_BACKEND
        check(LWS::Win32::BootstrapProcess() == LWS::Result::Success, "Cannot initialize Win32");
#endif
        LWS::PlatformContext platform;
        LWS::PlatformConfig config;
#ifdef LWS_HAS_WIN32_BACKEND
        config.backend = LWS::BackendId::Win32;
#else
        config.backend = LWS::BackendId::Wayland;
#endif
        check(platform.Init(config) == LWS::Result::Success, "Cannot initialize platform");
        LWS::Window window(platform);
        LWS::WindowConfig wc;
        wc.title = NativeText("ScrollView / Dynamic resolution with regular controls");
        wc.clientSize = {narrow ? 900 : 1180, narrow ? 600 : 740};
        wc.clientSizeLimits.minimum = {900, 600};
        wc.styles = LWS::WindowStyle::Caption | LWS::WindowStyle::CloseButton | LWS::WindowStyle::ResizableBorder;
        check(window.Create(wc) == LWS::Result::Success, "Cannot create window");
        UIHost host(window);
        host.SetRedrawOnResize(true);
        auto root = std::make_unique<DockPanel>();
        root->gap = 8;
        auto heading = std::make_unique<StackPanel>();
        auto& title = heading->Emplace<Label>("Two-axis ScrollView: 4,096 ordinary control cards");
        title.fontRole = Control::FontRole::Heading;
        auto population = std::make_unique<FlowPanel>();
        population->AddItem(std::make_unique<Label>("Grid side"), false, 70);
        auto& countSlider = static_cast<Slider&>(population->AddItem(std::make_unique<Slider>(), false, 320));
        countSlider.SetMaxWidth(320);
        auto& countValue = countSlider.ConfigureValue<int64_t>({16, 256, 1}, CardSide, SliderValueMode::ReadOnly);
        auto& countLabel = static_cast<Label&>(population->AddItem(std::make_unique<Label>("64 x 64 = 4096 cards"), false, 250));
        heading->Add(std::move(population));
        auto& instructions = heading->Emplace<Label>("Drag inward to reduce speed, or enable outward acceleration to increase speed outside the scrollbar. Wheel scrolls vertically, Shift+wheel horizontally. Buttons, checkboxes and text fields retain their state. Line mode uses an estimated scroll unit.");
        instructions.wrap = true;
        auto toolbar = std::make_unique<FlowPanel>();
        auto& dynamic = static_cast<CheckBox&>(toolbar->AddItem(std::make_unique<CheckBox>("Dynamic resolution"), false, 190));
        auto& guide = static_cast<CheckBox&>(toolbar->AddItem(std::make_unique<CheckBox>("Show guide"), false, 125));
        toolbar->AddItem(std::make_unique<Label>("Inward distance %"), false, 115);
        auto& distance = static_cast<NumericEdit<int64_t>&>(toolbar->AddItem(std::make_unique<NumericEdit<int64_t>>(NumericSpec<int64_t>{1, 100, 1}), false, 85));
        distance.SetMaxWidth(85);
        auto& precision = static_cast<ComboBox&>(toolbar->AddItem(std::make_unique<ComboBox>(std::vector<Choice>{{"Row unit (estimate)", "row"}, {"Pixel unit", "pixel"}}), false, 160));
        toolbar->AddItem(std::make_unique<Label>("Scroll speed"), false, 85);
        auto& progress = static_cast<Progress&>(toolbar->AddItem(std::make_unique<Progress>()));
        auto& speedLabel = static_cast<demo::SpeedReadout&>(toolbar->AddItem(std::make_unique<demo::SpeedReadout>(), false, 110));
        heading->Add(std::move(toolbar));
        auto acceleration = std::make_unique<FlowPanel>();
        auto& outward = static_cast<CheckBox&>(acceleration->AddItem(std::make_unique<CheckBox>("Outward acceleration"), false, 190));
        acceleration->AddItem(std::make_unique<Label>("Fine unit fraction %"), false, 120);
        auto& unitFraction = static_cast<NumericEdit<double>&>(acceleration->AddItem(std::make_unique<NumericEdit<double>>(NumericSpec<double>{.01, std::nullopt, .01}), false, 85));
        unitFraction.SetMaxWidth(85);
        acceleration->AddItem(std::make_unique<Label>("Maximum speed %"), false, 125);
        auto& maximumSpeed = static_cast<NumericEdit<int64_t>&>(acceleration->AddItem(std::make_unique<NumericEdit<int64_t>>(NumericSpec<int64_t>{100, 10000, 100}), false, 100));
        acceleration->AddItem(std::make_unique<Label>("Outward distance %"), false, 130);
        auto& outwardDistance = static_cast<NumericEdit<int64_t>&>(acceleration->AddItem(std::make_unique<NumericEdit<int64_t>>(NumericSpec<int64_t>{1, 100, 1}), false, 85));
        maximumSpeed.SetMaxWidth(100);
        outwardDistance.SetMaxWidth(85);
        heading->Add(std::move(acceleration));
        root->AddDocked(std::move(heading), Dock::Top);
        auto& status = static_cast<Label&>(root->AddDocked(std::make_unique<Label>("Interact with a card, then scroll away and return."), Dock::Bottom));
        auto view = std::make_unique<ScrollView>();
        auto* scroll = view.get();
        ScrollViewOptions options;
        options.horizontalScroll = options.dynamicResolution = options.resolutionGuide = true;
        options.resolution = ScrollResolution::Pixel;
        scroll->SetOptions(options);
        Button* firstButton = nullptr;
        Button* lastButton = nullptr;
        CheckBox* firstCheck = nullptr;
        TextBox* firstText = nullptr;
        size_t clicks = 0;
        std::vector<Event<void()>::Connection> buttons;
        size_t cardSide = CardSide;
        const auto rebuild = [&](size_t side)
        {
            auto cards = std::make_unique<Cards>(side);
            buttons.clear();
            firstButton = lastButton = nullptr;
            firstCheck = nullptr;
            firstText = nullptr;
            for (size_t i = 0; i < side * side; ++i)
            {
                auto stack = std::make_unique<StackPanel>();
                stack->Emplace<Label>("Card " + std::to_string(i / side + 1) + " / " + std::to_string(i % side + 1));
                if (i % 3 == 0)
                {
                    auto& button = stack->Emplace<Button>("Click me");
                    if (!firstButton) firstButton = &button;
                    lastButton = &button;
                    buttons.push_back(button.OnClick.Connect([&, i] { ++clicks; status.SetText("Clicked card " + std::to_string(i + 1) + ", total clicks " + std::to_string(clicks)); }));
                }
                else if (i % 3 == 1)
                {
                    auto& box = stack->Emplace<CheckBox>("Keep this value");
                    if (!firstCheck) firstCheck = &box;
                }
                else
                {
                    auto& text = stack->Emplace<TextBox>("Editable card text");
                    if (!firstText) firstText = &text;
                }
                auto panel = std::make_unique<Panel>(std::move(stack));
                panel->padding = Insets{8};
                cards->Add(std::move(panel));
            }
            scroll->SetContent(std::move(cards));
            cardSide = side;
            title.SetText("Two-axis ScrollView: " + std::to_string(side * side) + " ordinary control cards");
            status.SetText("Grid resized. Card values reset. Drag either scrollbar to explore.");
        };
        rebuild(CardSide);
        auto countConnection = countValue.OnEdit.Connect([&](int64_t value, EditPhase phase)
        {
            const auto side = size_t(value);
            countLabel.SetText(std::to_string(side) + " x " + std::to_string(side) + " = " + std::to_string(side * side) + " cards");
            // Rebuild only on release to keep slider dragging responsive.
            if (phase == EditPhase::Commit && side != cardSide) rebuild(side);
        });
        root->AddDocked(std::move(view), Dock::Fill);
        auto panel = std::make_unique<Panel>(std::move(root));
        panel->padding = Insets{8};
        host.SetRoot(std::move(panel));
        const auto sync = [&]
        {
            const auto& o = scroll->Options();
            dynamic.SetValue(o.dynamicResolution);
            guide.SetValue(o.resolutionGuide);
            guide.SetEnabled(o.dynamicResolution);
            distance.SetValue(int64_t(std::lround(o.inwardDistance * 100)));
            distance.SetEnabled(o.dynamicResolution);
            outward.SetValue(o.outwardAcceleration);
            outward.SetEnabled(o.dynamicResolution);
            unitFraction.SetValue(o.resolutionFraction * 100);
            unitFraction.SetEnabled(o.dynamicResolution);
            maximumSpeed.SetValue(int64_t(o.maximumScrollSpeed));
            outwardDistance.SetValue(int64_t(std::lround(o.outwardDistance * 100)));
            maximumSpeed.SetEnabled(o.dynamicResolution && o.outwardAcceleration);
            outwardDistance.SetEnabled(o.dynamicResolution && o.outwardAcceleration);
            progress.maximum = o.dynamicResolution && o.outwardAcceleration ? o.maximumScrollSpeed : 100;
            progress.Invalidate();
            precision.SetValue(o.resolution == ScrollResolution::Pixel ? "pixel" : "row");
            precision.SetEnabled(o.dynamicResolution);
        };
        auto dc = dynamic.OnChange.Connect([&](bool enabled) { auto o = scroll->Options(); o.dynamicResolution = enabled; scroll->SetOptions(o); sync(); });
        auto gc = guide.OnChange.Connect([&](bool enabled) { auto o = scroll->Options(); o.resolutionGuide = enabled; scroll->SetOptions(o); });
        auto distanceConnection = distance.OnEdit.Connect([&](int64_t value, EditPhase) { auto o = scroll->Options(); o.inwardDistance = double(value) / 100; scroll->SetOptions(o); });
        auto pc = precision.OnChange.Connect([&](const std::string& value) { auto o = scroll->Options(); o.resolution = value == "pixel" ? ScrollResolution::Pixel : ScrollResolution::Row; scroll->SetOptions(o); });
        auto outwardConnection = outward.OnChange.Connect([&](bool enabled)
        { auto o = scroll->Options(); o.outwardAcceleration = enabled; scroll->SetOptions(o); sync(); });
        auto unitFractionConnection = unitFraction.OnEdit.Connect([&](double value, EditPhase)
        { auto o = scroll->Options(); o.resolutionFraction = value / 100; scroll->SetOptions(o); });
        auto maximumSpeedConnection = maximumSpeed.OnEdit.Connect([&](int64_t value, EditPhase)
        { auto o = scroll->Options(); o.maximumScrollSpeed = double(value); scroll->SetOptions(o); progress.maximum = o.dynamicResolution && o.outwardAcceleration ? o.maximumScrollSpeed : 100; progress.Invalidate(); });
        auto outwardDistanceConnection = outwardDistance.OnEdit.Connect([&](int64_t value, EditPhase)
        { auto o = scroll->Options(); o.outwardDistance = double(value) / 100; scroll->SetOptions(o); });
        auto sc = scroll->OnScrollSpeed.Connect([&](double speed)
        {
            progress.speed = speed;
            progress.Invalidate();
            speedLabel.SetSpeed(speed);
        });
        sync();
        bool failed = false;
        auto error = host.OnError.Connect([&](const std::string& message) { std::cerr << message << '\n'; failed = true; });
        auto closing = window.Listen([&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventCloseRequested>(event))
            {
                host.Finish(EditPhase::Cancel);
                platform.RequestQuit();
                return LWS::EventResponse::Handled;
            }
            return LWS::EventResponse::Unhandled;
        });
        check(bool(closing), "Cannot register window listener");
        check(window.SetVisible(true) == LWS::Result::Success, "Cannot show window");
        host.Update();
        std::unique_ptr<LWS::Timer> timer;
        if (smoke)
        {
            timer = std::make_unique<LWS::Timer>(platform);
            check(timer->SetTargetWindow(&window) == LWS::Result::Success, "Cannot target timer");
            timer->SetCallback([&]
            {
                timer->SetInterval(0);
                try
                {
                    const auto click = [&](Control& control)
                    {
                        const auto b = control.Bounds();
                        host.Route({.kind = InputKind::Down, .x = b.x + 10, .y = b.y + b.height / 2});
                        host.Route({.kind = InputKind::Up, .x = b.x + 10, .y = b.y + b.height / 2});
                    };
                    if (snapshot) Snapshot(host, narrow ? "scroll-initial-narrow.ppm" : "scroll-initial.ppm");
                    click(*firstButton);
                    click(*firstCheck);
                    click(*firstText);
                    host.Route({.kind = InputKind::KeyDown, .key = LWS::KeyCode::End});
                    host.Route({.kind = InputKind::Text, .text = " edited"});
                    host.Focus(firstButton);
                    check(clicks == 1 && firstCheck->Value() && firstText->Text().ends_with(" edited"), "Card input failed");
                    scroll->SetScrollOffset(1000, 1000);
                    scroll->SetScrollOffset(0, 0);
                    host.Update();
                    check(firstCheck->Value() && firstText->Text().ends_with(" edited"), "Card state lost after scrolling");
                    check(host.Focus(firstText), "Cannot focus the text card");
                    scroll->SetScrollOffset(1000, 1000);
                    host.Update();
                    host.Route({.kind = InputKind::KeyDown, .key = LWS::KeyCode::End});
                    host.Route({.kind = InputKind::Text, .text = " hidden"});
                    check(host.Focus(firstButton), "Cannot commit the off-screen text card");
                    scroll->SetScrollOffset(float(cardSide) * 260, float(cardSide) * 120);
                    host.Update();
                    click(*lastButton);
                    check(clicks == 2, "Cannot interact with the final card");
                    scroll->SetScrollOffset(0, 0);
                    host.Update();
                    check(firstText->Text().ends_with(" edited hidden") && firstCheck->Value(), "Off-screen state was lost");
                    const auto resizeGrid = [&](double fraction, size_t expectedSide)
                    {
                        const auto track = countSlider.TrackBounds();
                        const float x = track.x + float(fraction) * (track.width - 1);
                        const float y = track.y + track.height / 2;
                        host.Route({.kind = InputKind::Down, .x = x, .y = y});
                        host.Route({.kind = InputKind::Move, .x = x, .y = y});
                        host.Route({.kind = InputKind::Up, .x = x, .y = y});
                        host.Update();
                        check(cardSide == expectedSide, "Grid size slider did not commit");
                        scroll->SetScrollOffset(float(cardSide) * 260, float(cardSide) * 120);
                        host.Update();
                        const auto previousClicks = clicks;
                        click(*lastButton);
                        check(clicks == previousClicks + 1, "Cannot interact with the resized final card");
                        scroll->SetScrollOffset(0, 0);
                        host.Update();
                    };
                    resizeGrid(1, 256);
                    resizeGrid(0, 16);
                    for (auto index : {size_t(0), size_t(1)})
                    {
                        auto* bar = static_cast<ScrollBar*>(scroll->Children()[index].get());
                        check(bar->Visible(), "Both scrollbars must be visible");
                        const auto b = bar->Bounds(), area = scroll->Bounds();
                        const bool vertical = index == 0;
                        const float x = b.x + b.width / 2, y = b.y + b.height / 2;
                        host.Route({.kind = InputKind::Down, .x = x, .y = y});
                        const float inwardX = vertical ? area.x : x, inwardY = vertical ? y : area.y;
                        host.Route({.kind = InputKind::Move, .x = inwardX, .y = inwardY});
                        check(bar->ResolutionDragging() && bar->ScrollSpeed() < 100, "Dynamic resolution did not engage");
                        if (snapshot)
                            for (auto theme : {ThemePreset::Dark, ThemePreset::Light, ThemePreset::Warm})
                            {
                                host.SetTheme(MakeTheme(theme));
                                Snapshot(host, "scroll-" + std::string(vertical ? "vertical-" : "horizontal-") + std::string(ThemePresetName(theme)) + (narrow ? "-narrow.ppm" : ".ppm"));
                            }
                        host.Route({.kind = InputKind::Up, .x = inwardX, .y = inwardY});
                        check(!bar->ResolutionDragging() && bar->ScrollSpeed() == 100, "Drag did not finish");
                        auto acceleratedOptions = scroll->Options();
                        acceleratedOptions.outwardAcceleration = true;
                        scroll->SetOptions(acceleratedOptions);
                        sync();
                        host.Update();
                        host.Route({.kind = InputKind::Down, .x = x, .y = y});
                        const float outwardX = vertical ? b.x + b.width + area.width * .1f : x;
                        const float outwardY = vertical ? y : b.y + b.height + area.height * .1f;
                        host.Route({.kind = InputKind::Move, .x = outwardX, .y = outwardY});
                        check(bar->ResolutionDragging() && bar->ScrollSpeed() > 399, "Outward acceleration did not engage");
                        check(progress.speed > 399 && progress.maximum == 400, "Outward feedback did not update");
                        if (snapshot)
                            for (auto theme : {ThemePreset::Dark, ThemePreset::Light, ThemePreset::Warm})
                            {
                                host.SetTheme(MakeTheme(theme));
                                Snapshot(host, "scroll-outward-" + std::string(vertical ? "vertical-" : "horizontal-") + std::string(ThemePresetName(theme)) + (narrow ? "-narrow.ppm" : ".ppm"));
                            }
                        host.Route({.kind = InputKind::Up, .x = outwardX, .y = outwardY});
                        check(bar->ScrollSpeed() == 100 && progress.speed == 100, "Outward feedback did not reset");
                        acceleratedOptions.outwardAcceleration = false;
                        scroll->SetOptions(acceleratedOptions);
                        sync();
                        host.Update();
                    }
                }
                catch (const std::exception& e) { std::cerr << e.what() << '\n'; failed = true; }
                platform.RequestQuit();
            });
            timer->SetInterval(500);
        }
        const auto result = platform.RunMessageLoop();
        return failed || result == LWS::LoopResult::Failed ? 1 : 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
