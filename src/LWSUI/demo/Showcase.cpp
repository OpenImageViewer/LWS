#include "Showcase.hpp"
#include "SplitPreview.hpp"
#include <LWSUI/Composites.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>
#ifdef LWS_HAS_WIN32_BACKEND
    #include <LWS/Win32/WindowExtensions.hpp>
#endif

namespace LWSUI::demo
{
    namespace
    {
        void Check(LWS::Result result, const char* operation)
        {
            if (result != LWS::Result::Success)
                throw std::runtime_error(operation);
        }
        Label& Text(Container& parent, std::string text, bool heading = false)
        {
            auto& label = parent.Emplace<Label>(std::move(text));
            label.wrap = true;
            if (heading)
                label.fontRole = Control::FontRole::Heading;
            return label;
        }
        // A padded container that gives its content the entire remaining viewport.
        class Frame : public Container
        {
          public:

            Frame(std::unique_ptr<Control> body, std::string title)
            {
                title_ = &Emplace<Label>(std::move(title));
                title_->wrap = true;
                title_->fontRole = FontRole::Heading;
                body_ = &Add(std::move(body));
            }
            void SetTitle(std::string title) { title_->SetText(std::move(title)); }
            bool OnPreviewInput(const Input& input) override { return body_->PreviewInput(input); }

          protected:

            Size OnMeasure(Size available) override
            {
                title_->Measure({std::max(1.f, available.width - 20), available.height});
                body_->Measure({std::max(1.f, available.width - 20), std::max(1.f, available.height - 60)});
                return available;
            }
            void OnArrange() override
            {
                const auto b = Bounds();
                const float h = title_->DesiredSize().height;
                title_->Arrange({b.x + 10, b.y + 8, std::max(0.f, b.width - 20), h});
                body_->Arrange({b.x + 10, b.y + h + 16, std::max(0.f, b.width - 20), std::max(0.f, b.height - h - 26)});
            }
            void OnRender(Canvas& c) override
            {
                const auto b = Bounds();
                c.Fill(b.x, b.y, b.width, b.height, Style().surface);
                Container::OnRender(c);
            }

          private:

            Label* title_;
            Control* body_;
        };
        // Fix the viewport height of an ordinary control without allocating a native window.
        class Viewport : public Container
        {
          public:

            Viewport(std::unique_ptr<Control> content, float height, float width = 0) : height_(height), width_(width)
            {
                content_ = &Add(std::move(content));
            }

          protected:

            Size OnMeasure(Size available) override
            {
                const Size size{width_ > 0 ? std::min(width_, available.width) : available.width, height_};
                content_->Measure(size);
                return size;
            }
            void OnArrange() override { content_->Arrange(Bounds()); }

          private:

            float height_, width_;
            Control* content_;
        };
        // A compact two-row arrangement when wide, with the same controls stacked when narrow.
        class QuickControls final : public StackPanel
        {
          protected:

            Size OnMeasure(Size available) override
            {
                if (available.width < 360)
                    return StackPanel::OnMeasure(available);
                for (auto& child : Children())
                    child->Measure({available.width, available.height});
                return {available.width,
                        std::max(Children()[0]->DesiredSize().height, Children()[3]->DesiredSize().height) + 6 +
                            std::max(Children()[1]->DesiredSize().height, Children()[2]->DesiredSize().height)};
            }
            void OnArrange() override
            {
                const auto b = Bounds();
                if (b.width < 360)
                {
                    StackPanel::OnArrange();
                    return;
                }
                const float first = std::max(Children()[0]->DesiredSize().height, Children()[3]->DesiredSize().height);
                const float second = std::max(Children()[1]->DesiredSize().height, Children()[2]->DesiredSize().height);
                Children()[0]->Arrange({b.x, b.y, b.width - 126, first});
                Children()[3]->Arrange({b.x + b.width - 120, b.y, 120, first});
                Children()[1]->Arrange({b.x, b.y + first + 6, 70, second});
                Children()[2]->Arrange({b.x + 78, b.y + first + 6, b.width - 78, second});
            }
        };
        struct PreviewState
        {
            std::string title = "A connected showcase", scene = "Circles", quality = "Balanced", file;
            std::string notes = "Scene notes\nEnter adds a line. Ctrl+Enter commits; Escape restores the draft.\n\n"
                                "Resize this gallery to see wrapping, or add more lines to scroll.";
            double opacity = .85, scale = 1;
            int64_t count = 5;
            bool enabled = true;
            LLUtils::Color color{uint32_t{0x66b7ffff}};
        };
        class Preview : public Control
        {
          public:

            explicit Preview(PreviewState& state) : state_(state) {}

          protected:

            Size OnMeasure(Size available) override
            {
                const int columns = std::clamp(int((available.width - 32) / 64), 1, 4);
                const float rows = std::ceil(float(state_.count) / columns);
                const float title =
                    Host()->MeasureText(state_.title, std::max(1.f, available.width - 32), true, Font()).height;
                return {available.width, std::max(available.height, std::max(100.f, title + 48 + rows * 24))};
            }
            void OnRender(Canvas& c) override
            {
                const auto b = Bounds();
                const auto& s = state_;
                c.Fill(b.x, b.y, b.width, b.height, Style().background);
                const float titleHeight = c.TextHeight(s.title, std::max(1.f, b.width - 32), Font(), true);
                c.Text(s.title, b.x + 16, b.y + 16, std::max(1.f, b.width - 32), titleHeight, Style().foreground,
                       Font(), true);
                const int columns = std::clamp(int((b.width - 32) / 64), 1, 4);
                const float rows = std::ceil(float(s.count) / columns);
                const float maximum = std::max(4.f, std::min((b.width - 32) / (2 * columns) - 8,
                                                             (b.height - titleHeight - 48) / (2 * rows) - 7));
                const float radius = std::clamp(float(18 * s.scale), 4.f, maximum);
                const auto color = s.enabled
                                       ? LLUtils::Color(s.color.R(), s.color.G(), s.color.B(), uint8_t(s.opacity * 255))
                                       : Style().muted;
                for (int64_t i = 0; i < s.count; ++i)
                {
                    const float x = b.x + 16 + (float(i % columns) + .5f) * (b.width - 32) / columns;
                    const float y = b.y + titleHeight + 32 + radius + float(i / columns) * (2 * radius + 14);
                    if (s.scene == "Tiles")
                        c.Fill(x - radius, y - radius, radius * 2, radius * 2, color);
                    else
                    {
                        c.FillCircle(x, y, radius, color);
                        c.StrokeCircle(x, y, radius, 1, Style().foreground);
                    }
                }
            }

          private:

            PreviewState& state_;
        };
        struct Surface
        {
            // Reverse destruction order disconnects subscriptions, then destroys host, then window.
            std::unique_ptr<LWS::Window> window;
            std::unique_ptr<UIHost> host;
            ConnectionSet connections;
            std::vector<LWS::EventConnection> listeners;
        };
        void Place(Surface& surface, Rect bounds, bool visible)
        {
            auto& window = *surface.window;
            if (!window.IsCreated())
                return;
            visible = visible && bounds.width >= 1 && bounds.height >= 1;
            if (visible)
            {
                const LWS::Point position{int32_t(std::lround(bounds.x)), int32_t(std::lround(bounds.y))};
                const LWS::LogicalSize size{int32_t(std::lround(bounds.width)), int32_t(std::lround(bounds.height))};
                const auto old = window.GetPlacement();
                if (old.position != position || old.clientSize != size)
                    Check(window.RequestPlacement({.position = position, .clientSize = size}),
                          "Cannot place child pane");
            }
            if (window.IsVisible() != visible)
                Check(window.SetVisible(visible), "Cannot change child visibility");
        }
        class Workspace : public Container
        {
          public:

            Workspace(Surface& gallery, Surface& preview, Surface& fixed, std::function<void(std::string)> action)
                : gallery_(gallery), preview_(preview), fixed_(fixed)
            {
                auto& toolbar = Emplace<StackPanel>();
                toolbar.orientation = Orientation::Horizontal;
                for (auto name : {"Controls Gallery", "Menu Gallery", "Reset layout"})
                {
                    auto& button = toolbar.Emplace<Button>(name);
                    connections_.Connect(button.OnClick, [action, name] { action(name); });
                }
                toolbar_ = &toolbar;
                note_ = &Emplace<Label>("Drag empty menu-bar space to move the window. The outer divider resizes "
                                        "native panes; inner dividers resize containers.");
                note_->wrap = true;
                status_ = &Emplace<Label>();
                split_ = &Emplace<SplitPanel>(std::make_unique<Control>(), std::make_unique<Control>(),
                                              Orientation::Horizontal);
                split_->SetRatio(.4f);
                split_->SetMinimumSizes(260, 220);
                split_->SetDividerSize(10);
            }
            void Reset() { split_->Reset(); }

          protected:

            Size OnMeasure(Size size) override
            {
                toolbar_->Measure({size.width - 24, 40});
                note_->Measure({size.width - 24, 70});
                status_->Measure({size.width - 24, 50});
                return size;
            }
            void OnArrange() override
            {
                const auto b = Bounds();
                const float th = toolbar_->DesiredSize().height, nh = note_->DesiredSize().height;
                toolbar_->Arrange({b.x + 12, b.y + 10, std::max(0.f, b.width - 24), th});
                note_->Arrange({b.x + 12, b.y + th + 18, std::max(0.f, b.width - 24), nh});
                const float start = th + nh + 24;
                content_ = {b.x, b.y + start, b.width, std::max(0.f, b.height - start - 42)};
                const auto c = content_;
                split_->Measure({std::max(0.f, c.width - 24), std::max(0.f, c.height - 24)});
                split_->Arrange({c.x + 12, c.y + 12, std::max(0.f, c.width - 24), std::max(0.f, c.height - 24)});
                auto left = split_->First().Bounds();
                const bool showFixed = left.width >= 240 && left.height >= 260;
                const float top = std::max(0.f, left.height - (showFixed ? 172.f : 0.f));
                Place(gallery_, {left.x, left.y, left.width, top}, true);
                Place(preview_, split_->Second().Bounds(), true);
                Place(fixed_, {left.x, left.y + top + 12, 240, 160}, showFixed);
                status_->SetText(showFixed ? "Drag divider or focus it and use Left / Right. Home restores the split."
                                           : "Expand this window to show the fixed 240 x 160 pane.");
                status_->Arrange({b.x + 12, b.y + std::max(0.f, b.height - 38), std::max(0.f, b.width - 24), 34});
            }

          private:

            Surface &gallery_, &preview_, &fixed_;
            Control* toolbar_;
            Label *note_, *status_;
            ConnectionSet connections_;
            Rect content_;
            SplitPanel* split_;
        };
        struct Image
        {
            int width, height;
            std::vector<std::byte> pixels;
        };
        Image Render(UIHost& host)
        {
            host.Update();
            const auto size = host.Window().GetClientAreaMetrics().logical;
            Canvas canvas;
            canvas.Begin(size.x, size.y);
            canvas.Fill(0, 0, float(size.x), float(size.y), host.Style().background);
            if (host.Root())
                host.Root()->Render(canvas);
            if (host.MainMenu())
                host.MainMenu()->Render(canvas);
            const auto buffer = canvas.End();
            Image result{size.x, size.y, std::vector<std::byte>(size_t(size.x) * size.y * 4)};
            for (int y = 0; y < size.y; ++y)
                std::copy_n(buffer.pixels.data() + y * buffer.rowPitch, size.x * 4,
                            result.pixels.data() + y * size.x * 4);
            return result;
        }
        void Save(const Image& image, const std::string& name)
        {
            std::ofstream out(name, std::ios::binary);
            out << "P6\n" << image.width << ' ' << image.height << "\n255\n";
            for (size_t i = 0; i < image.pixels.size(); i += 4)
            {
                const char rgb[]{char(image.pixels[i + 2]), char(image.pixels[i + 1]), char(image.pixels[i])};
                out.write(rgb, 3);
            }
            if (!out)
                throw std::runtime_error("Cannot save showcase snapshot");
        }
    }  // namespace
    struct Showcase::Impl
    {
        Showcase& owner;
        LWS::PlatformContext& platform;
        UIHost& menuHost;
        PreviewState state;
        Surface main, controls, gallery, preview, fixed, events;
        ConnectionSet menuConnections;
        std::vector<Sample> samples;
        std::vector<Label*> sections;
        std::vector<std::string> history;
        Workspace* workspace = nullptr;
        ScrollView *catalogScroll = nullptr, *containerScroll = nullptr;
        Label *historyLabel = nullptr, *values = nullptr;
        Preview* drawing = nullptr;
        SplitPreview* splitPreview = nullptr;
        Label *details = nullptr, *inspector = nullptr, *activity = nullptr;
        CheckBox* quickEnabled = nullptr;
        Slider* quickOpacity = nullptr;
        ThemePreset themePreset = DefaultThemePreset;
        TextBox *name = nullptr, *notes = nullptr, *eventOutput = nullptr;
        CheckBox* enabled = nullptr;
        Slider *opacity = nullptr, *filled = nullptr;
        NumericEdit<int64_t>* count = nullptr;
        NumericEdit<double>* scale = nullptr;
        ComboBox* scene = nullptr;
        RadioGroup* quality = nullptr;
        ColorPicker* color = nullptr;
        ColorSwatch *swatch = nullptr, *catalogSwatch = nullptr;
        RadioButton *circleRadio = nullptr, *tileRadio = nullptr;
        FilePicker* file = nullptr;
        std::shared_ptr<bool> alive = std::make_shared<bool>(true);
        bool failed = false, liveResize = false;

        Impl(Showcase& owner, LWS::PlatformContext& platform, UIHost& menuHost)
            : owner(owner), platform(platform), menuHost(menuHost)
        {
            menuHost.SetTheme(MakeTheme(DefaultThemePreset));
            Check(menuHost.Window().SetBackgroundColor(MakeTheme(DefaultThemePreset).background),
                  "Cannot set gallery background");
            Make(main, "LWSUI / Workspace - resizable", {1100, 760}, nullptr, false, 0);
            Make(controls, "LWSUI / Controls Gallery - resizable", {600, 720}, nullptr, false, 1);
            Make(events, "LWSUI / Events", {560, 420}, nullptr, false, 2);
            BuildEvents();
            Make(gallery, "Container Gallery", {360, 380}, main.window.get());
            Make(preview, "Live Preview", {500, 580}, main.window.get());
            Make(fixed, "Fixed Sample", {240, 160}, main.window.get(), true);
            BuildContainers();
            BuildControls();
            BuildPreview();
            BuildMenuGallery();
            auto layout = std::make_unique<Workspace>(gallery, preview, fixed,
                                                      [this](auto command) { Command(command); });
            workspace = layout.get();
            samples.push_back({"Container", workspace});
            main.host->SetRoot(std::move(layout));
            main.host->SetMainMenu(std::make_unique<MenuBar>(ApplicationMenus()));
            controls.host->SetMainMenu(std::make_unique<MenuBar>(ApplicationMenus()));
            main.host->MainMenu()->SetWindowControls({.minimize = true,
                                                      .maximize = true,
                                                      .draggable = true,
                                                      .requestClose = [this] { CloseSurface(main, true); },
                                                      .doubleClickMaximize = true});
            ListenClose(main, true);
            ListenClose(controls, false);
            ListenClose(events, false);
            ListenCloseMenu();
#ifdef LWS_HAS_WIN32_BACKEND
            if (const auto monitor = platform.GetPrimaryMonitor())
            {
                const auto origin = monitor->workRect.GetCorner(LLUtils::TopLeft);
                const double sx = std::max(1.0, monitor->contentScale.x), sy = std::max(1.0, monitor->contentScale.y);
                const int width = int(monitor->workRect.GetWidth() / sx),
                          height = int(monitor->workRect.GetHeight() / sy);
                Check(menuHost.Window().RequestPlacement(
                          {.position = LWS::Point{int(origin.x / sx) + std::max(0, width - 580),
                                                  int(origin.y / sy) + std::max(0, height - 660)}}),
                      "Cannot position menu gallery");
            }
#endif
            Check(main.window->SetVisible(true), "Cannot show main workspace");
            Check(controls.window->SetVisible(true), "Cannot show controls gallery");
            Check(events.window->SetVisible(true), "Cannot show event viewer");
            main.host->Update();
            Refresh();
        }
        ~Impl()
        {
            *alive = false;
            menuListeners.clear();
            menuConnections.Clear();
            menuHost.SetRoot(nullptr);
            // Surface members and declarations already encode child-before-parent teardown.
        }
        void Deferred(std::function<void()> action)
        {
            const auto weak = std::weak_ptr(alive);
            Check(platform.PostTask(
                      [weak, action = std::move(action)]
                      {
                          if (const auto token = weak.lock(); token && *token)
                              action();
                      }),
                  "Cannot schedule showcase action");
        }
        void Make(Surface& surface, const char* title, LWS::LogicalSize size, LWS::Window* parent = nullptr,
                  bool fixedSize = false, int offset = 0)
        {
            surface.window = std::make_unique<LWS::Window>(platform);
            LWS::WindowConfig config;
            config.title = NativeText(title);
            config.parent = parent;
            config.clientSize = size;
            config.backgroundColor = MakeTheme(DefaultThemePreset).background;
            if (!parent)
            {
                config.styles = LWS::WindowStyle::Caption | LWS::WindowStyle::CloseButton |
                                LWS::WindowStyle::ResizableBorder | LWS::WindowStyle::MaximizeButton;
                config.clientSizeLimits.minimum = offset == 0 ? LWS::LogicalSize{720, 520} : LWS::LogicalSize{400, 340};
#ifdef LWS_HAS_WIN32_BACKEND
                if (const auto monitor = platform.GetPrimaryMonitor())
                {
                    const double sx = monitor->contentScale.x > 0 ? monitor->contentScale.x : 1;
                    const double sy = monitor->contentScale.y > 0 ? monitor->contentScale.y : 1;
                    const auto origin = monitor->workRect.GetCorner(LLUtils::TopLeft);
                    const int width = int(monitor->workRect.GetWidth() / sx),
                              height = int(monitor->workRect.GetHeight() / sy);
                    config.clientSize.x = std::min(size.x, std::max(400, width - 80));
                    config.clientSize.y = std::min(size.y, std::max(340, height - 100));
                    config.position = LWS::Point{
                        int(origin.x / sx) + (offset == 0 ? 20 : std::max(20, width - config.clientSize.x - 20)),
                        int(origin.y / sy) + 30};
                }
#endif
            }
            if (&surface == &main)
                config.styles = LWS::WindowStyle::ResizableBorder;
            if (fixedSize)
                config.clientSizeLimits = {size, size};
            Check(surface.window->Create(config), "Cannot create showcase window");
            if (parent)
            {
                auto keys = surface.window->Listen(
                    [this](const LWS::AnyEvent& event)
                    {
                        Input input{InputKind::KeyDown};
                        if (const auto* key = std::get_if<LWS::EventKeyDown>(&event))
                        {
                            input.key = key->key;
                            input.repeat = key->repeat;
                            input.shift = platform.IsKeyPressed(LWS::KeyCode::Shift).value_or(false);
                            if (!main.host->HasOpenMenu() &&
                                (input.shift || (key->key != LWS::KeyCode::F10 && key->key != LWS::KeyCode::Alt)))
                                return LWS::EventResponse::Unhandled;
                        }
                        else if (const auto* key = std::get_if<LWS::EventKeyUp>(&event))
                        {
                            input.kind = InputKind::KeyUp;
                            input.key = key->key;
                        }
                        else if (const auto* text = std::get_if<LWS::EventTextInput>(&event))
                        {
                            input.kind = InputKind::Text;
                            input.text = text->text;
                        }
                        else
                            return LWS::EventResponse::Unhandled;
                        if (input.kind != InputKind::KeyDown && !main.host->HasOpenMenu())
                            return LWS::EventResponse::Unhandled;
                        return main.host->Route(input) ? LWS::EventResponse::Handled : LWS::EventResponse::Unhandled;
                    });
                if (!keys)
                    throw std::runtime_error("Cannot listen for pane keyboard input");
                surface.listeners.push_back(std::move(*keys));
            }
            surface.host = std::make_unique<UIHost>(*surface.window, MakeTheme(DefaultThemePreset));
            surface.connections.Connect(surface.host->OnError,
                                        [this](const std::string& message)
                                        {
                                            failed = true;
                                            Log("Error: " + message);
                                        });
        }
        void CloseSurface(Surface& surface, bool primary)
        {
            if (primary)
                platform.RequestQuit();
            else
            {
                surface.host->CloseMainMenu();
                surface.host->Finish(EditPhase::Cancel);
                Check(surface.window->SetVisible(false), "Cannot hide companion");
            }
        }
        void ListenClose(Surface& surface, bool primary)
        {
            auto listener = surface.window->Listen(
                [this, &surface, primary](const LWS::AnyEvent& event)
                {
                    if (std::holds_alternative<LWS::EventCloseRequested>(event))
                    {
                        Deferred([this, &surface, primary] { CloseSurface(surface, primary); });
                        return LWS::EventResponse::Handled;
                    }
                    if (primary && std::holds_alternative<LWS::EventWindowDestroyed>(event))
                        platform.RequestQuit();
                    return LWS::EventResponse::Unhandled;
                });
            if (!listener)
                throw std::runtime_error("Cannot listen for showcase close");
            surface.listeners.push_back(std::move(*listener));
        }
        std::vector<LWS::EventConnection> menuListeners;
        void ListenCloseMenu()
        {
            auto listener = menuHost.Window().Listen(
                [this](const LWS::AnyEvent& event)
                {
                    if (!std::holds_alternative<LWS::EventCloseRequested>(event))
                        return LWS::EventResponse::Unhandled;
                    Deferred(
                        [this]
                        {
                            menuHost.CloseMainMenu();
                            menuHost.Finish(EditPhase::Cancel);
                            Check(menuHost.Window().SetVisible(false), "Cannot hide menu gallery");
                        });
                    return LWS::EventResponse::Handled;
                });
            if (!listener)
                throw std::runtime_error("Cannot listen for gallery close");
            menuListeners.push_back(std::move(*listener));
        }
        std::vector<MenuBarItem> ApplicationMenus()
        {
            const auto action = [this](std::string name) { return [this, name] { Command(name); }; };
            return {
                {"&Scene",
                 {{"&Circles", "", action("Circles")},
                  {"&Tiles", "", action("Tiles")},
                  MenuItem::Separator(),
                  {"&Reset values", "", action("Reset values")}}},
                {"&Window",
                 {{"&Controls Gallery", "", action("Controls Gallery")},
                  {"&Menu Gallery", "", action("Menu Gallery")},
                  {"&Events", "", action("Events")},
                  {"Reset &layout", "", action("Reset layout")},
                  {"&Live resize redraw", "", action("Live resize redraw"), true, liveResize}}},
                {"&Theme",
                 {{"&Dark", "", [this] { owner.SetTheme(ThemePreset::Dark); }, true, themePreset == ThemePreset::Dark},
                  {"&Light", "", [this] { owner.SetTheme(ThemePreset::Light); }, true,
                   themePreset == ThemePreset::Light},
                  {"&Warm", "", [this] { owner.SetTheme(ThemePreset::Warm); }, true, themePreset == ThemePreset::Warm}}},
                {"&Help", {{"&About this showcase", "", action("About")}}}};
        }
        void BuildEvents()
        {
            auto content = std::make_unique<DockPanel>();
            content->gap = 8;
            auto toolbar = std::make_unique<FlowPanel>();
            auto heading = std::make_unique<Label>("Events / latest 200");
            heading->fontRole = Control::FontRole::Heading;
            toolbar->AddItem(std::move(heading), true);
            auto clear = std::make_unique<Button>("Clear log");
            clear->sizeToContent = true;
            events.connections.Connect(clear->OnClick,
                                       [this]
                                       {
                                           history.clear();
                                           eventOutput->SetText("");
                                           if (activity)
                                               activity->SetText("No events yet.");
                                           if (historyLabel)
                                               historyLabel->SetText("No events yet.");
                                       });
            toolbar->AddItem(std::move(clear));
            content->AddDocked(std::move(toolbar), Dock::Top);
            auto viewer = std::make_unique<TextBox>("", TextBoxMode::Multiline);
            eventOutput = viewer.get();
            eventOutput->SetReadOnly(true);
            eventOutput->fontRole = Control::FontRole::Small;
            eventOutput->SetPlaceholder("Events appear here. Select and copy text, or scroll through the history.");
            content->AddDocked(std::move(viewer), Dock::Fill);
            auto frame = std::make_unique<Panel>(std::move(content));
            frame->padding = Insets{12};
            events.host->SetRoot(std::move(frame));
        }
        void Log(const std::string& message)
        {
            history.push_back(message);
            if (history.size() > 200)
                history.erase(history.begin());
            std::string recent;
            size_t count = 0;
            for (auto it = history.rbegin(); it != history.rend() && count < 18; ++it, ++count)
                recent += *it + '\n';
            if (activity)
                activity->SetText(recent);
            if (historyLabel)
                historyLabel->SetText(recent);
            if (eventOutput)
            {
                std::string output;
                for (const auto& entry : history)
                    output += entry + '\n';
                eventOutput->SetText(std::move(output));
            }
        }
        void Refresh()
        {
            // Sync shared, non-text values in one direction. Text drafts remain owned by their editors.
            for (auto* control : {enabled, quickEnabled})
                if (control && control->Value() != state.enabled)
                    control->SetValue(state.enabled);
            for (auto* control : {opacity, filled, quickOpacity})
                if (control && control->Value() != state.opacity)
                    control->SetValue(state.opacity);
            if (scene && scene->Value() != state.scene)
                scene->SetValue(state.scene);
            if (details)
                details->SetText(
                    state.scene + " | " + state.quality + " | " + std::to_string(state.count) + " shapes\n" +
                    (state.file.empty() ? "No file selected" : state.file) +
                    "\nCustom Control / Canvas. All five containers share one native window.\n" +
                    "Reset Split resets only these containers. Reset Layout resets both inner and outer splits.");
            if (drawing)
                drawing->Invalidate(true);
            if (swatch)
                swatch->SetValue(state.color);
            if (catalogSwatch)
                catalogSwatch->SetValue(state.color);
            if (circleRadio)
                circleRadio->SetValue(state.scene == "Circles");
            if (tileRadio)
                tileRadio->SetValue(state.scene == "Tiles");
            if (values)
            {
                std::ostringstream text;
                text << state.scene << " | " << state.count << " shapes | opacity " << std::fixed
                     << std::setprecision(2) << state.opacity << " | scale " << state.scale << " | "
                     << (state.enabled ? "enabled" : "disabled");
                values->SetText(text.str());
                if (inspector)
                    inspector->SetText("Spans Drawing + Details.\n\n" + text.str() +
                                       "\n\nDrag the shared divider to resize Drawing and Details together.");
            }
        }
        void Command(const std::string& name)
        {
            if (name == "Controls Gallery" || name == "Menu Gallery" || name == "Events")
            {
                auto& target = name == "Controls Gallery" ? *controls.window
                               : name == "Events"         ? *events.window
                                                          : menuHost.Window();
                Check(target.SetVisible(true), "Cannot reopen gallery");
                const auto activated = target.RequestActivation();
                if (activated != LWS::Result::Success && activated != LWS::Result::NotSupported)
                    Log("Activation unavailable");
            }
            else if (name == "Live resize redraw")
            {
                liveResize = !liveResize;
                for (auto* surface : {&main, &controls, &gallery, &preview, &fixed, &events})
                    surface->host->SetRedrawOnResize(liveResize);
                menuHost.SetRedrawOnResize(liveResize);
                main.host->MainMenu()->SetItems(ApplicationMenus());
                controls.host->MainMenu()->SetItems(ApplicationMenus());
            }
            else if (name == "Reset layout")
            {
                workspace->Reset();
                splitPreview->ResetSplit();
            }
            else if (name == "Reset values" || name == "file/new" || name == "file/save")
                Reset();
            else if (name == "Circles" || name == "Tiles")
            {
                state.scene = name;
            }
            else if (name == "About" || name == "help/about")
            {
                state.title = "LWSUI: native panes, shared controls, independent hosts";
                this->name->SetText(state.title);
            }
            else
            {
                state.title = "Selected: " + name;
                this->name->SetText(state.title);
                if (name.find("file/") == 0)
                {
                    state.scene = "Tiles";
                }
            }
            Refresh();
            Log(name);
        }
        void Reset()
        {
            // Cancel drafts and color-popup previews before publishing the reset snapshot.
            if (!controls.host->Finish(EditPhase::Cancel) || !preview.host->Finish(EditPhase::Cancel))
                return;
            state = {};
            name->SetText(state.title);
            name->SetValidation("");
            notes->SetText(state.notes);
            count->SetValue(state.count);
            scale->SetValue(state.scale);
            quality->SetValue(state.quality);
            color->SetValue(state.color);
            file->SetValue("");
            Refresh();
        }
        Label& Section(StackPanel& parent, std::string title, std::string description)
        {
            auto& label = Text(parent, std::move(title), true);
            sections.push_back(&label);
            Text(parent, std::move(description));
            return label;
        }
        StackPanel& ExampleCard(StackPanel& parent, std::string title, std::string description)
        {
            auto content = std::make_unique<StackPanel>();
            auto& body = *content;
            body.spacing = 6;
            Text(body, std::move(title), true);
            if (!description.empty())
                Text(body, std::move(description)).fontRole = Control::FontRole::Small;
            auto& panel = parent.Emplace<Panel>(std::move(content));
            panel.padding = Insets{12};
            panel.border = Insets{1};
            panel.paintBackground = false;
            return body;
        }
        template <class T>
        T& PlaceControl(Container& parent, std::unique_ptr<T> control, bool framed = false)
        {
            T* pointer = control.get();
            if (framed)
            {
                auto& frame = parent.Emplace<Panel>(std::move(control));
                frame.border = Insets{1};
                frame.padding = Insets{1};
            }
            else
                parent.Add(std::move(control));
            return *pointer;
        }
        template <class T>
        T& SampleControl(Container& parent, std::string_view type, std::unique_ptr<T> control, bool framed = false)
        {
            auto& sample = PlaceControl(parent, std::move(control), framed);
            samples.push_back({type, &sample});
            return sample;
        }
        void BuildInputExamples(StackPanel& list)
        {
            Section(list, "02 / Input policies",
                    "TextBox filters reject invalid edits. Fixed masks insert separators and keep empty slots "
                    "visible.");
            using Validator = std::function<std::string(const TextBox&)>;
            const auto example = [this, &list](std::string instructions, InputFilter filter, std::string_view mask = {},
                                               Validator validate = {}, std::string initial = {})
            {
                const auto colon = instructions.find(':');
                auto& card = ExampleCard(list, instructions.substr(0, colon) + " / TextBox",
                                         instructions.substr(colon + 2));
                auto& field = PlaceControl(card, std::make_unique<TextBox>(), true);
                field.SetInputFilter(std::move(filter));
                field.SetText(std::move(initial));
                if (!mask.empty() && !field.SetInputMask(mask))
                    throw std::logic_error("Invalid gallery mask");
                auto& feedback = Text(card, "Invalid edits keep the current value and show a red underline.");
                feedback.fontRole = Control::FontRole::Small;
                controls.connections.Connect(field.OnInputRejected,
                                             [&feedback](const std::string& error) { feedback.SetText(error); });
                controls.connections.Connect(field.OnEdit,
                                             [&field, &feedback, validate](const std::string&, EditPhase)
                                             {
                                                 if (validate)
                                                     field.SetValidation(validate(field));
                                                 feedback.SetText(field.Validation().empty() ? "Input accepted"
                                                                                             : field.Validation());
                                             });
            };
            example(
                "Integer: optional minus and digits. Empty text and minus are editable drafts. "
                "Commit requires a complete integer.",
                InputFilters::Number(true), {}, [](const TextBox& field)
                { return internal::ParseNumericText<int64_t>(field.Text()) ? "" : "Enter a complete integer"; }, "42");
            example(
                "Decimal: a decimal point and exponent are allowed, for example -1.25e-3. "
                "Commit requires a finite number.",
                InputFilters::Number(), {},
                [](const TextBox& field)
                {
                    const auto number = internal::ParseNumericText<double>(field.Text());
                    return number && std::isfinite(*number) ? "" : "Enter a complete finite number";
                },
                "1.25");
            const HexadecimalFormFlags hexForms{HexadecimalForm::Bare | HexadecimalForm::HashPrefix |
                                                HexadecimalForm::ZeroXPrefix};
            example(
                "Hexadecimal: accepts FF, #FF, 0xFF, and 0XFF. Prefix flags select the allowed forms. Letter case is "
                "preserved.",
                InputFilters::Hexadecimal(hexForms), {},
                [hexForms](const TextBox& field)
                {
                    if (field.Text().empty())
                        return std::string{};
                    const auto complete = internal::CheckHexadecimal(field.Text(), hexForms, true);
                    return complete ? std::string{} : complete.error();
                },
                "a0FF");
            example(
                "Hex color: start with # and enter six RGB or eight RGBA hexadecimal digits.",
                InputFilters::Hexadecimal(true), {}, [](const TextBox& field)
                { return field.Text().size() == 7 || field.Text().size() == 9 ? "" : "Use #RRGGBB or #RRGGBBAA"; },
                "#336699");
            example("Date: YYYY-MM-DD. Separators appear automatically. Calendar validity is checked "
                    "separately from the digit mask.",
                    {}, "0000-00-00",
                    [](const TextBox& field)
                    {
                        const auto& value = field.Text();
                        if (value.empty() || !field.MaskComplete())
                            return std::string{};
                        const std::chrono::year_month_day day{
                            std::chrono::year(std::stoi(value.substr(0, 4))),
                            std::chrono::month(unsigned(std::stoi(value.substr(5, 2)))),
                            std::chrono::day(unsigned(std::stoi(value.substr(8, 2))))};
                        return std::string(day.ok() ? "" : "Enter a valid calendar date");
                    });
            example("Time: HH:MM in 24-hour time. The mask inserts the colon. Validation checks hours "
                    "and minutes.",
                    {}, "00:00",
                    [](const TextBox& field)
                    {
                        const auto& value = field.Text();
                        const bool valid = value.empty() || !field.MaskComplete() ||
                                           (std::stoi(value.substr(0, 2)) < 24 && std::stoi(value.substr(3, 2)) < 60);
                        return std::string(valid ? "" : "Use hours 00-23 and minutes 00-59");
                    });
            example("Phone: ten digits formatted as (000) 000-0000. Type digits or paste a formatted number.", {},
                    "(000) 000-0000");
            example("Product ID: two ASCII letters and four digits, formatted as AA-0000. Letter case is retained. "
                    "Typing overwrites slots. Backspace and Delete clear slots without moving later fields.",
                    {}, "AA-0000");
        }
        void BuildControls()
        {
            auto list = std::make_unique<StackPanel>();
            list->spacing = 12;
            Text(*list, "Every control has a named example. Changes below update the Workspace preview.");
            auto& index = list->Emplace<FlowPanel>();
            const std::array<const char*, 5> names{"Text", "Input policies", "Choices", "Values", "Files / color"};
            for (size_t i = 0; i < names.size(); ++i)
            {
                auto entry = std::make_unique<Button>(names[i]);
                entry->sizeToContent = true;
                auto& button = *entry;
                index.AddItem(std::move(entry));
                controls.connections.Connect(button.OnClick,
                                             [this, i]
                                             {
                                                 if (i < sections.size())
                                                     catalogScroll->SetOffset(catalogScroll->Offset() +
                                                                              sections[i]->Bounds().y -
                                                                              catalogScroll->Bounds().y);
                                             });
            }
            Section(*list, "01 / Text and actions",
                    "Label wraps with the window. TextBox supports selection, clipboard, validation, and Escape to "
                    "cancel.");
            auto& label = SampleControl(
                ExampleCard(*list, "Label", "Display-only text wraps with the available width."), "Label",
                std::make_unique<Label>("Label: resize this gallery to see a long explanation "
                                        "wrap without creating another native window."));
            label.wrap = true;
            name = &SampleControl(ExampleCard(*list, "TextBox / Single line",
                                              "Edit the preview title. Enter commits and Escape cancels."),
                                  "TextBox", std::make_unique<TextBox>(state.title), true);
            name->SetPlaceholder("Enter a preview title");
            controls.connections.Connect(name->OnEdit,
                                         [this](const std::string& value, EditPhase phase)
                                         {
                                             const bool valid = value.size() <= 80;
                                             name->SetValidation(valid ? "" : "Use at most 80 bytes for this title");
                                             if (valid)
                                                 state.title = value;
                                             Refresh();
                                             if (phase != EditPhase::Preview)
                                                 Log("Title edit finished");
                                         });
            auto& notesCard = ExampleCard(*list, "TextBox / Multiline",
                                          "Enter adds a line. Ctrl+Enter commits. Escape restores the draft.");
            notes = &PlaceControl(notesCard, std::make_unique<TextBox>(state.notes, TextBoxMode::Multiline), true);
            notes->SetVisibleLines(4);
            notes->SetPlaceholder("Add notes about this scene...");
            controls.connections.Connect(notes->OnEdit,
                                         [this](const std::string& value, EditPhase phase)
                                         {
                                             state.notes = value;
                                             if (phase != EditPhase::Preview)
                                                 Log(phase == EditPhase::Cancel ? "Notes restored" : "Notes committed");
                                         });
            auto& readonly = PlaceControl(ExampleCard(*list, "TextBox / Read-only",
                                                      "Select and copy text without editing it."),
                                          std::make_unique<TextBox>("Select and copy this explanation."), true);
            readonly.SetReadOnly(true);
            auto& borderless = ExampleCard(*list, "TextBox / Borderless",
                                           "Editable text without the usual field surface or frame.")
                                   .Emplace<TextBox>("This line is editable.");
            borderless.SetBorderless(true);
            auto& empty = PlaceControl(ExampleCard(*list, "TextBox / Placeholder",
                                                   "An empty field displays a format hint."),
                                       std::make_unique<TextBox>(), true);
            empty.SetPlaceholder("An empty TextBox with placeholder text");
            auto& actions = ExampleCard(*list, "Button / Normal, flat, and disabled",
                                        "Reset changes shared values. The flat button selects the Tiles scene.");
            auto& reset = SampleControl(actions, "Button", std::make_unique<Button>("Reset values"));
            reset.focusLossPhase = EditPhase::Cancel;
            controls.connections.Connect(reset.OnClick, [this] { Command("Reset values"); });
            auto& flat = actions.Emplace<Button>("Flat: show Tiles");
            flat.flat = true;
            controls.connections.Connect(flat.OnClick, [this] { Command("Tiles"); });
            actions.Emplace<Button>("Disabled").SetEnabled(false);
            BuildInputExamples(*list);
            Section(*list, "03 / Choices",
                    "CheckBox toggles the preview. Standalone RadioButtons are paired by this app; RadioGroup manages "
                    "a set itself.");
            enabled = &SampleControl(ExampleCard(*list, "CheckBox",
                                                 "Toggle preview colors. Right-click for a context menu."),
                                     "CheckBox", std::make_unique<CheckBox>("Enable preview colors"));
            enabled->SetValue(true);
            controls.connections.Connect(enabled->OnChange,
                                         [this](bool value)
                                         {
                                             state.enabled = value;
                                             Refresh();
                                             Log("Enabled state changed");
                                         });
            enabled->SetContextMenuProvider(
                [this](Control&, const ContextMenuRequest&)
                {
                    return std::vector<ContextMenuItem>{{"Reset enabled", "", [this](Control&)
                                                         {
                                                             state.enabled = true;
                                                             Refresh();
                                                             Log("Context menu: enabled");
                                                         }}};
                });
            auto& radios = ExampleCard(*list, "RadioButton / Standalone pair",
                                       "The application keeps these two choices exclusive.");

            auto& circles = SampleControl(radios, "RadioButton", std::make_unique<RadioButton>("Circles"));
            auto& tiles = radios.Emplace<RadioButton>("Tiles");
            circles.SetValue(true);
            circleRadio = &circles;
            tileRadio = &tiles;
            controls.connections.Connect(circles.OnChange,
                                         [this](bool on)
                                         {
                                             if (on)
                                             {
                                                 Command("Circles");
                                             }
                                         });
            controls.connections.Connect(tiles.OnChange,
                                         [this](bool on)
                                         {
                                             if (on)
                                             {
                                                 Command("Tiles");
                                             }
                                         });
            quality = &SampleControl(
                ExampleCard(*list, "RadioGroup", "The group manages an exclusive selection of quality levels."),
                "RadioGroup",
                std::make_unique<RadioGroup>(
                    std::vector<Choice>{{"Fast", "Fast"}, {"Balanced", "Balanced"}, {"High", "High quality"}}));
            quality->orientation = Orientation::Vertical;
            quality->SetValue(state.quality);
            controls.connections.Connect(quality->OnChange,
                                         [this](const std::string& value)
                                         {
                                             state.quality = value;
                                             Refresh();
                                             Log("Quality: " + value);
                                         });
            scene = &SampleControl(
                ExampleCard(*list, "ComboBox", "Choose a scene from the popup, using the mouse or keyboard."),
                "ComboBox",
                std::make_unique<ComboBox>(std::vector<Choice>{{"Circles", "Circles"}, {"Tiles", "Tiles"}}));
            scene->SetValue(state.scene);
            controls.connections.Connect(scene->OnChange, [this](const std::string& value) { Command(value); });
            Section(*list, "04 / Numbers, sliders, and scrolling",
                    "NumericEdit: type a value or hold the rocker. Sliders preview while dragging; Escape restores the "
                    "previous value.");
            count = &SampleControl(ExampleCard(*list, "NumericEdit / Integer",
                                               "Shape count from 1 to 12. Type a value or hold a rocker button."),
                                   "NumericEdit<int64_t>",
                                   std::make_unique<NumericEdit<int64_t>>(NumericSpec<int64_t>{1, 12, 1}), true);
            count->SetValue(state.count);
            controls.connections.Connect(count->OnEdit,
                                         [this](int64_t value, EditPhase)
                                         {
                                             state.count = value;
                                             Refresh();
                                         });
            scale = &SampleControl(
                ExampleCard(*list, "NumericEdit / Decimal", "Shape scale from 0.5 to 2.0 in 0.1 steps."),
                "NumericEdit<double>", std::make_unique<NumericEdit<double>>(NumericSpec<double>{.5, 2, .1}), true);
            scale->SetValue(state.scale);
            controls.connections.Connect(scale->OnEdit,
                                         [this](double value, EditPhase)
                                         {
                                             state.scale = value;
                                             Refresh();
                                         });
            auto& sliderCard = ExampleCard(*list, "Slider / Outline and filled",
                                           "Both sliders change preview opacity. Escape cancels a drag.");
            Text(sliderCard, "Outline track with editable value").fontRole = Control::FontRole::Small;
            opacity = &SampleControl(sliderCard, "Slider", std::make_unique<Slider>());
            opacity->SetValue(state.opacity);
            opacity->SetValueDisplay(SliderValueMode::Editable);
            Text(sliderCard, "Filled track with read-only value").fontRole = Control::FontRole::Small;
            filled = &sliderCard.Emplace<Slider>();
            filled->filled = true;
            filled->SetValueDisplay(SliderValueMode::ReadOnly);
            filled->SetValue(state.opacity);
            controls.connections.Connect(opacity->OnEdit,
                                         [this](double value, EditPhase)
                                         {
                                             state.opacity = value;
                                             Refresh();
                                         });
            controls.connections.Connect(filled->OnEdit,
                                         [this](double value, EditPhase)
                                         {
                                             state.opacity = value;
                                             Refresh();
                                         });
            auto& scrollCard = ExampleCard(*list, "ScrollBar / Standalone",
                                           "Move the thumb to inspect lines in a fixed-height viewport.");
            auto scrollGrid = std::make_unique<Grid>();
            scrollGrid->columns = {-1, 22};
            auto& viewportText = scrollGrid->Emplace<Label>(
                "Sample line 1\nSample line 2\nSample line 3\nSample line 4");
            viewportText.wrap = true;
            auto& scrollbar = SampleControl(*scrollGrid, "ScrollBar", std::make_unique<ScrollBar>());
            scrollbar.SetPage(.2);
            controls.connections.Connect(scrollbar.OnEdit,
                                         [&viewportText](double value, EditPhase)
                                         {
                                             const int first = 1 + int(value * 16);
                                             std::string text;
                                             for (int i = first; i < first + 4; ++i)
                                                 text += "Sample line " + std::to_string(i) + "\n";
                                             viewportText.SetText(text);
                                         });
            scrollCard.Emplace<Viewport>(std::move(scrollGrid), 110);
            Section(*list, "05 / Color and files",
                    "ColorSwatch is a standalone button. ColorPicker previews RGB/HSL edits and restores the color on "
                    "Cancel.");
            auto& sample = SampleControl(ExampleCard(*list, "ColorSwatch",
                                                     "Click the standalone color button to select amber."),
                                         "ColorSwatch", std::make_unique<ColorSwatch>());
            sample.SetValue(state.color);
            catalogSwatch = &sample;
            controls.connections.Connect(sample.OnClick,
                                         [this]
                                         {
                                             state.color = LLUtils::Color{uint32_t{0xf6a44cff}};
                                             color->SetValue(state.color);
                                             Refresh();
                                             Log("Swatch: amber");
                                         });
            color = &SampleControl(ExampleCard(*list, "ColorPicker",
                                               "Edit a hex color or open the RGB/HSL popup. Cancel restores the "
                                               "previous color."),
                                   "ColorPicker", std::make_unique<ColorPicker>(), true);
            color->SetValue(state.color);
            controls.connections.Connect(color->OnEdit,
                                         [this](LLUtils::Color value, EditPhase)
                                         {
                                             state.color = value;
                                             Refresh();
                                         });
            file = &SampleControl(ExampleCard(*list, "FilePicker",
                                              "Edit an existing file path, click Browse, or press F4. The selected "
                                              "file is never modified."),
                                  "FilePicker", std::make_unique<FilePicker>(), true);
            controls.connections.Connect(file->OnEdit,
                                         [this](const std::string& value, EditPhase)
                                         {
                                             state.file = value;
                                             Refresh();
                                         });
            auto scroll = std::make_unique<ScrollView>();
            catalogScroll = scroll.get();
            scroll->overlayScrollBar = false;
            scroll->SetContent(std::move(list));
            controls.host->SetRoot(std::make_unique<Frame>(std::move(scroll), "Controls Gallery / resizable window"));
        }
        void BuildContainers()
        {
            auto content = std::make_unique<StackPanel>();
            content->spacing = 12;
            samples.push_back({"StackPanel", content.get()});
            Text(*content, "One native child window, many ordinary controls", true);
            Text(*content, "These nested containers share this pane's native surface. Only the three labeled panes are "
                           "separate native windows.");
            auto surface = std::make_unique<Panel>();
            samples.push_back({"Panel", surface.get()});
            auto dock = std::make_unique<DockPanel>();
            samples.push_back({"DockPanel", dock.get()});
            auto flow = std::make_unique<FlowPanel>();
            samples.push_back({"FlowPanel", flow.get()});
            auto glyph = std::make_unique<GlyphButton>(Glyph::DoubleChevronUp);
            samples.push_back({"GlyphButton", glyph.get()});
            glyph->SetTooltip("Scroll to the top");
            gallery.connections.Connect(glyph->OnClick, [this] { containerScroll->SetOffset(0); });
            flow->AddItem(std::move(glyph));
            flow->AddItem(std::make_unique<Label>("FlowPanel wraps intrinsic controls"));
            dock->AddDocked(std::move(flow), Dock::Fill);
            surface->SetContent(std::move(dock));
            content->Add(std::move(surface));
            auto picture = std::make_unique<LWSUI::Image>();
            samples.push_back({"Image", picture.get()});
            const std::array<std::byte, 16> pixels{std::byte{40},  std::byte{140}, std::byte{230}, std::byte{255},
                                                   std::byte{230}, std::byte{140}, std::byte{40},  std::byte{255},
                                                   std::byte{230}, std::byte{140}, std::byte{40},  std::byte{255},
                                                   std::byte{40},  std::byte{140}, std::byte{230}, std::byte{255}};
            picture->SetBitmap(std::make_shared<LWS::Bitmap>(
                LWS::BitmapBuffer{.pixels = pixels, .width = 2, .height = 2, .rowPitch = 8}));
            content->Add(std::move(picture));
            Text(*content, "Horizontal StackPanel: children share the available width.");
            auto& horizontal = content->Emplace<StackPanel>();
            horizontal.orientation = Orientation::Horizontal;
            for (auto name : {"Circles", "Tiles"})
            {
                auto& button = horizontal.Emplace<Button>(name);
                gallery.connections.Connect(button.OnClick, [this, name] { Command(name); });
            }
            Text(*content, "Grid: a fixed 90-pixel label column and a proportional value column.");
            auto& grid = SampleControl(*content, "Grid", std::make_unique<Grid>());
            grid.columns = {90, -1};
            for (auto [label, value] :
                 {std::pair{"Layout", "Fixed + flexible columns"}, {"Resize", "Watch the second column expand"}})
            {
                Text(grid, label);
                Text(grid, value);
            }
            Text(*content, "TreeView / Branch / Row: expand nested groups and align property rows.");
            auto& tree = SampleControl(*content, "TreeView", std::make_unique<TreeView>());
            auto& branch = SampleControl(tree, "TreeView::Branch",
                                         std::make_unique<TreeView::Branch>("Container anatomy"));
            SampleControl(branch.Content(), "TreeView::Row",
                          std::make_unique<TreeView::Row>(std::make_unique<Label>("Windows"),
                                                          std::make_unique<Label>("1")));
            auto& nested = branch.Content().Emplace<TreeView::Branch>("Nested StackPanel");
            Text(nested.Content(), "Labels, buttons, grids, and branches do not allocate individual HWNDs.");
            gallery.connections.Connect(branch.OnExpanded,
                                        [this](bool on) { Log(on ? "Branch expanded" : "Branch collapsed"); });
            Text(*content, "Fixed-height ScrollView / overlay scrollbar: scroll here independently of the outer pane.");
            auto inner = std::make_unique<ScrollView>();
            inner->overlayScrollBar = true;
            auto rows = std::make_unique<StackPanel>();
            for (int i = 1; i <= 16; ++i)
                Text(*rows, "Nested scroll row " + std::to_string(i));
            inner->SetContent(std::move(rows));
            content->Emplace<Viewport>(std::move(inner), 140);
            Text(*content,
                 "Outer ScrollView / reserved scrollbar: all sections remain reachable when this native pane shrinks.");
            auto scroll = std::make_unique<ScrollView>();
            containerScroll = scroll.get();
            samples.push_back({"ScrollView", scroll.get()});
            scroll->SetContent(std::move(content));
            SetPaneRoot(gallery, std::move(scroll), "Containers / resizable native child");
        }
        void SetPaneRoot(Surface& surface, std::unique_ptr<Control> body, std::string name)
        {
            auto frame = std::make_unique<Frame>(std::move(body), name);
            auto* pointer = frame.get();
            surface.host->SetRoot(std::move(frame));
            auto resized = surface.window->Listen(
                [pointer, name](const LWS::AnyEvent& event)
                {
                    if (const auto* size = std::get_if<LWS::EventClientAreaSizeChanged>(&event))
                        pointer->SetTitle(name + " / " + std::to_string(size->size.logical.x) + " x " +
                                          std::to_string(size->size.logical.y));
                    return LWS::EventResponse::Unhandled;
                });
            if (!resized)
                throw std::runtime_error("Cannot observe pane dimensions");
            surface.listeners.push_back(std::move(*resized));
        }
        void BuildPreview()
        {
            auto body = std::make_unique<Preview>(state);
            drawing = body.get();
            samples.push_back({"Control", drawing});
            auto information = std::make_unique<Label>();
            information->wrap = true;
            details = information.get();
            auto quick = std::make_unique<QuickControls>();
            quickEnabled = &quick->Emplace<CheckBox>("Enabled");
            preview.connections.Connect(quickEnabled->OnChange,
                                        [this](bool value)
                                        {
                                            state.enabled = value;
                                            Refresh();
                                            Log(state.enabled ? "Quick Controls: enabled" : "Quick Controls: disabled");
                                        });
            Text(*quick, "Opacity");
            quickOpacity = &quick->Emplace<Slider>();
            quickOpacity->filled = true;
            preview.connections.Connect(quickOpacity->OnEdit,
                                        [this](double value, EditPhase phase)
                                        {
                                            state.opacity = value;
                                            Refresh();
                                            if (phase != EditPhase::Preview)
                                                Log(phase == EditPhase::Cancel ? "Opacity restored"
                                                                               : "Opacity changed");
                                        });
            auto& reset = quick->Emplace<Button>("Reset values");
            reset.focusLossPhase = EditPhase::Cancel;
            preview.connections.Connect(reset.OnClick, [this] { Command("Reset values"); });
            auto inspectorContent = std::make_unique<Label>();
            inspectorContent->wrap = true;
            inspector = inspectorContent.get();
            auto activityContent = std::make_unique<Label>("No commands yet.\nTry Quick Controls.");
            activityContent->wrap = true;
            activity = activityContent.get();
            auto panes = std::make_unique<SplitPreview>(
                std::array<std::unique_ptr<Control>, 5>{std::move(body), std::move(information), std::move(quick),
                                                        std::move(inspectorContent), std::move(activityContent)});
            splitPreview = panes.get();
            samples.push_back({"SplitPanel", splitPreview->Divider(1).Parent()});
            SetPaneRoot(preview, std::move(panes), "Live preview / one native child");
            auto fixedBody = std::make_unique<Grid>();
            fixedBody->columns = {60, -1};
            swatch = &fixedBody->Emplace<ColorSwatch>();
            swatch->SetValue(state.color);
            fixed.connections.Connect(swatch->OnClick, [this] { Command("Controls Gallery"); });
            auto& button = fixedBody->Emplace<Button>("Show Controls");
            fixed.connections.Connect(button.OnClick, [this] { Command("Controls Gallery"); });
#ifdef LWS_HAS_WIN32_BACKEND
            Text(*fixedBody, "HWND");
#else
            Text(*fixedBody, "Child");
#endif
            Text(*fixedBody, "240 x 160. Fixed size.");
            SetPaneRoot(fixed, std::move(fixedBody), "Fixed native child");
        }
        void BuildMenuGallery()
        {
            auto list = std::make_unique<StackPanel>();
            list->spacing = 10;
            Text(*list, "Fixed-size window / flexible contents", true);
            Text(*list, "This window stays 560 x 620 logical pixels. Scroll to reach the full explanation and command "
                        "history.");
            Text(*list, "Try the vertical menu on the left. View > Menu bar demonstrates horizontal, vertical, docked, "
                        "floating, and detached menu bars.");
            Text(*list, "Drag floating menus by the dotted grip. Their dropdowns are separate popup windows, so they "
                        "can extend beyond the bar.");
            auto& actions = list->Emplace<StackPanel>();
            actions.orientation = Orientation::Horizontal;
            for (const auto* name : {"Controls Gallery", "Reset values"})
            {
                auto& button = actions.Emplace<Button>(name);
                menuConnections.Connect(button.OnClick, [this, name] { Command(name); });
            }
            Text(*list, "Keyboard: Alt or F10 opens a menu; arrows navigate; Enter runs a command; Escape closes it. "
                        "Shift+F10 opens a control's context menu.");
            values = &Text(*list, "");
            Text(*list, "Recent commands / newest first", true);
            historyLabel = &Text(*list, "Choose a scene, change a value, or run a menu command.");
            auto scroll = std::make_unique<ScrollView>();
            scroll->SetContent(std::move(list));
            menuHost.SetRoot(std::make_unique<Frame>(std::move(scroll), "Menu Gallery / fixed 560 x 620"));
        }
        void Snapshot()
        {
            for (auto* surface : {&main, &controls, &gallery, &preview, &fixed, &events})
                surface->host->Update();
            Save(Render(*controls.host), "controls.ppm");
            Save(Render(menuHost), "menu-gallery.ppm");
            Save(Render(*events.host), "events.ppm");
            const bool preferred = splitPreview->PrefersSideBySide();
            splitPreview->SetSideBySide(true);
            preview.host->Update();
            Save(Render(*preview.host), "preview-side-by-side.ppm");
            splitPreview->SetSideBySide(false);
            preview.host->Update();
            Save(Render(*preview.host), "preview-stacked.ppm");
            splitPreview->SetSideBySide(preferred);
            preview.host->Update();
            auto composite = Render(*main.host);
            for (auto [surface, filename] : {std::pair{&gallery, "container-pane.ppm"},
                                             {&preview, "preview-pane.ppm"},
                                             {&fixed, "fixed-pane.ppm"}})
            {
                if (!surface->window->IsVisible())
                    continue;
                auto image = Render(*surface->host);
                Save(image, filename);
                const auto position = surface->window->GetPlacement().position;
                if (!position)
                    continue;
                for (int y = 0; y < image.height; ++y)
                    for (int x = 0; x < image.width; ++x)
                    {
                        const int dx = position->x + x, dy = position->y + y;
                        if (dx >= 0 && dy >= 0 && dx < composite.width && dy < composite.height)
                            std::copy_n(image.pixels.data() + (y * image.width + x) * 4, 4,
                                        composite.pixels.data() + (dy * composite.width + dx) * 4);
                    }
            }
            Save(composite, "workspace.ppm");
            // Capture both scrolling catalogs; their complete content is larger than a viewport.
            const auto pages = [](UIHost& host, ScrollView& scroll, const std::string& prefix)
            {
                const float previous = scroll.Offset();
                const float extent = std::max(0.f, scroll.Content()->DesiredSize().height - scroll.Bounds().height);
                const float page = std::max(1.f, scroll.Bounds().height - 30);
                int number = 0;
                for (float offset = 0;; offset = std::min(extent, offset + page))
                {
                    scroll.SetOffset(offset);
                    host.Update();
                    Save(Render(host), prefix + std::to_string(++number) + ".ppm");
                    if (offset >= extent)
                        break;
                }
                scroll.SetOffset(previous);
                host.Update();
            };
            pages(*controls.host, *catalogScroll, "catalog-");
            pages(*gallery.host, *containerScroll, "containers-");
            const auto originalTheme = themePreset;
            for (const auto palette : {ThemePreset::Light, ThemePreset::Dark, ThemePreset::Warm})
            {
                owner.SetTheme(palette);
                controls.host->Update();
                const std::string suffix = ThemePresetName(palette);
                Save(Render(*controls.host), "controls-" + suffix + ".ppm");
                pages(*controls.host, *catalogScroll, "catalog-" + suffix + "-");
            }
            owner.SetTheme(originalTheme);
        }
    };
    Showcase::Showcase(LWS::PlatformContext& platform, UIHost& host)
        : impl_(std::make_unique<Impl>(*this, platform, host))
    {
        impl_->samples.push_back({"MenuBar", impl_->main.host->MainMenu()});
    }
    Showcase::~Showcase() = default;
    void ReportShowcaseError(const std::string& message, bool interactive)
    {
#ifdef LWS_HAS_WIN32_BACKEND
        OutputDebugStringA(("LWSUI showcase: " + message + "\n").c_str());
        if (interactive)
            MessageBoxW(nullptr, NativeText(message).c_str(), L"LWSUI showcase", MB_OK | MB_ICONERROR);
#else
        std::cerr << "LWSUI showcase: " << message << '\n';
        (void) interactive;
#endif
    }
    void Showcase::Log(std::string message)
    {
        impl_->Log(message);
    }
    UIHost& Showcase::EventsHost() const
    {
        return *impl_->events.host;
    }
    void Showcase::Command(std::string name)
    {
        impl_->Command(name);
    }
    void Showcase::Reset()
    {
        impl_->Reset();
    }
    void Showcase::SetTheme(ThemePreset preset)
    {
        impl_->themePreset = preset;
        const auto theme = MakeTheme(preset);
        impl_->menuHost.SetTheme(theme);
        for (auto* surface : {&impl_->main, &impl_->controls, &impl_->gallery, &impl_->preview, &impl_->fixed})
            surface->host->SetTheme(theme);
        impl_->main.host->MainMenu()->SetItems(impl_->ApplicationMenus());
        impl_->controls.host->MainMenu()->SetItems(impl_->ApplicationMenus());
        for (auto* surface : {&impl_->main, &impl_->controls, &impl_->gallery, &impl_->preview, &impl_->fixed})
            Check(surface->window->SetBackgroundColor(theme.background), "Cannot change window background");
        Check(impl_->menuHost.Window().SetBackgroundColor(theme.background), "Cannot change menu background");
        if (OnThemeChanged)
            OnThemeChanged(preset);
        impl_->Log("Theme changed across all windows");
    }
    void Showcase::Snapshot()
    {
        impl_->Snapshot();
    }
    std::span<const Sample> Showcase::Samples() const
    {
        return impl_->samples;
    }
    LWS::Window& Showcase::MainWindow()
    {
        return *impl_->main.window;
    }
    SplitPreview& Showcase::PreviewContainers()
    {
        return *impl_->splitPreview;
    }
    bool Showcase::Verify()
    {
        bool result = !impl_->failed && Samples().size() == 29;
        for (const auto& sample : Samples())
            result = result && sample.control && sample.control->Host();
        result = result && impl_->fixed.window->GetClientAreaMetrics().logical == LWS::LogicalSize{240, 160};
        for (auto* child : {&impl_->gallery, &impl_->preview, &impl_->fixed})
        {
            result = result && child->window->GetParent() == impl_->main.window.get();
#ifdef LWS_HAS_WIN32_BACKEND
            if (const auto hwnd = LWS::Win32::GetHwnd(*child->window))
                result = result && (GetWindowLongW(*hwnd, GWL_STYLE) & WS_CHILD) != 0 &&
                         GetParent(*hwnd) == *LWS::Win32::GetHwnd(*impl_->main.window);
            else
                result = false;
#endif
        }
        impl_->Log("Showcase verification: " + std::to_string(Samples().size()) + " catalog entries, " +
                   (result ? "passed" : "FAILED"));
        return result;
    }
}  // namespace LWSUI::demo
