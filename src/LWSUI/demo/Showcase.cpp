#include "Showcase.hpp"
#include "SplitPreview.hpp"
#include <LWSUI/Composites.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#ifdef LWS_HAS_WIN32_BACKEND
    #include <LWS/Win32/WindowExtensions.hpp>
#endif

namespace LWSUI::demo
{
    WorkspaceGeometry ArrangeWorkspace(Rect b, float ratio)
    {
        constexpr float gap = 12, divider = 10;
        b = {b.x + gap, b.y + gap, std::max(0.f, b.width - 2 * gap), std::max(0.f, b.height - 2 * gap)};
        const float usable = std::max(0.f, b.width - divider);
        const float left = usable >= 480 ? std::clamp(usable * ratio, 260.f, usable - 220.f) : usable * .5f;
        const bool fixed = left >= 240 && b.height >= 260;
        const float top = std::max(0.f, b.height - (fixed ? 172.f : 0.f));
        return {{b.x, b.y, left, top},
                {b.x + left + divider, b.y, std::max(0.f, usable - left), b.height},
                {b.x, b.y + top + gap, 240, 160},
                {b.x + left, b.y, divider, b.height},
                fixed};
    }
    namespace
    {
        void Check(LWS::Result result, const char* operation)
        {
            if (result != LWS::Result::Success)
                throw std::runtime_error(operation);
        }
        struct Connections
        {
            template <class E, class F>
            void Add(E& event, F callback)
            {
                values.push_back(std::make_shared<typename E::Connection>(event.Connect(std::move(callback))));
            }
            std::vector<std::shared_ptr<void>> values;
        };
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
            Connections connections;
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
                    connections_.Add(button.OnClick, [action, name] { action(name); });
                }
                toolbar_ = &toolbar;
                note_ = &Emplace<Label>("Drag empty menu-bar space to move the window. The outer divider resizes "
                                        "native panes; inner dividers resize containers.");
                note_->wrap = true;
                status_ = &Emplace<Label>();
            }
            void Reset()
            {
                ratio_ = .4f;
                Invalidate(true);
            }
            void PointerCursor(std::optional<LWS::Point> point)
            {
                const bool over = dragging_ || (point && geometry_.divider.Contains(float(point->x), float(point->y)));
                const auto shape = over ? LWS::CursorShape::SizeEW : LWS::CursorShape::Arrow;
                if (shape != cursor_ && Host())
                {
                    cursor_ = shape;
                    std::ignore = Host()->Window().SetMouseCursor(LWS::Cursor::FromShape(shape));
                }
            }
            bool Focusable() const override { return true; }

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
                geometry_ = ArrangeWorkspace(content_, ratio_);
                Place(gallery_, geometry_.gallery, true);
                Place(preview_, geometry_.preview, true);
                Place(fixed_, geometry_.fixed, geometry_.showFixed);
                status_->SetText(geometry_.showFixed
                                     ? "Drag divider or focus it and use Left / Right. Home restores the split."
                                     : "Expand this window to show the fixed 240 x 160 pane.");
                status_->Arrange({b.x + 12, b.y + std::max(0.f, b.height - 38), std::max(0.f, b.width - 24), 34});
            }
            void OnRender(Canvas& c) override
            {
                Container::OnRender(c);
                const auto d = geometry_.divider;
                c.Fill(d.x + 2, d.y, std::max(1.f, d.width - 4), d.height, dragging_ ? Style().accent : Style().line);
            }
            bool OnInput(const Input& input) override
            {
                if (input.kind == InputKind::Down && geometry_.divider.Contains(input.x, input.y))
                {
                    dragging_ = true;
                    Capture();
                    return true;
                }
                if (input.kind == InputKind::Move && dragging_)
                {
                    ratio_ = std::clamp((input.x - content_.x - 12) / std::max(1.f, content_.width - 34), .05f, .95f);
                    Invalidate(true);
                    return true;
                }
                if (input.kind == InputKind::Up || input.kind == InputKind::Cancel || input.kind == InputKind::Blur)
                {
                    if (dragging_)
                    {
                        dragging_ = false;
                        ReleaseCapture();
                        Invalidate();
                        return true;
                    }
                }
                if (input.kind == InputKind::KeyDown && Host() && Host()->IsFocused(*this))
                {
                    if (input.key == LWS::KeyCode::Home)
                        Reset();
                    else if (input.key == LWS::KeyCode::Left || input.key == LWS::KeyCode::Right)
                    {
                        ratio_ = std::clamp(ratio_ + (input.key == LWS::KeyCode::Left ? -.03f : .03f), .05f, .95f);
                        Invalidate(true);
                    }
                    else
                        return false;
                    return true;
                }
                return false;
            }

          private:

            Surface &gallery_, &preview_, &fixed_;
            Control* toolbar_;
            Label *note_, *status_;
            Connections connections_;
            WorkspaceGeometry geometry_;
            Rect content_;
            float ratio_ = .4f;
            bool dragging_ = false;
            LWS::CursorShape cursor_ = LWS::CursorShape::Arrow;
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
        Surface main, controls, gallery, preview, fixed;
        Connections menuConnections;
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
        TextBox *name = nullptr, *notes = nullptr;
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
        bool failed = false;

        Impl(Showcase& owner, LWS::PlatformContext& platform, UIHost& menuHost)
            : owner(owner), platform(platform), menuHost(menuHost)
        {
            menuHost.SetTheme(MakeTheme(DefaultThemePreset));
            Check(menuHost.Window().SetBackgroundColor(MakeTheme(DefaultThemePreset).background),
                  "Cannot set gallery background");
            Make(main, "LWSUI / Workspace - resizable", {1100, 760}, nullptr, false, 0);
            Make(controls, "LWSUI / Controls Gallery - resizable", {600, 720}, nullptr, false, 1);
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
                                                      .requestClose = [this] { CloseSurface(main, true); }});
            ListenClose(main, true);
            ListenClose(controls, false);
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
            main.host->Update();
            Refresh();
        }
        ~Impl()
        {
            *alive = false;
            menuListeners.clear();
            menuConnections.values.clear();
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
            auto cursorListener = surface.window->Listen(
                [this, &surface](const LWS::AnyEvent& event)
                {
                    std::optional<LWS::Point> point;
                    if (const auto* move = std::get_if<LWS::EventMouseMove>(&event))
                        point = move->position;
                    else if (!std::holds_alternative<LWS::EventMouseLeave>(event))
                        return LWS::EventResponse::Unhandled;
                    if (&surface == &main && workspace)
                        workspace->PointerCursor(point);
                    if (&surface == &preview && splitPreview)
                        splitPreview->PointerCursor(point);
                    return LWS::EventResponse::Unhandled;
                });
            if (!cursorListener)
                throw std::runtime_error("Cannot observe divider hover");
            surface.listeners.push_back(std::move(*cursorListener));
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
            surface.connections.Add(surface.host->OnError,
                                    [this](const std::string& message)
                                    {
                                        failed = true;
                                        std::cerr << "Showcase: " << message << '\n';
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
                  {"Reset &layout", "", action("Reset layout")}}},
                {"&Theme",
                 {{"&Dark", "", [this] { owner.SetTheme(ThemePreset::Dark); }, true, themePreset == ThemePreset::Dark},
                  {"&Light", "", [this] { owner.SetTheme(ThemePreset::Light); }, true,
                   themePreset == ThemePreset::Light},
                  {"&Warm", "", [this] { owner.SetTheme(ThemePreset::Warm); }, true, themePreset == ThemePreset::Warm}}},
                {"&Help", {{"&About this showcase", "", action("About")}}}};
        }
        void Log(const std::string& message)
        {
            history.push_back(message);
            if (history.size() > 18)
                history.erase(history.begin());
            std::string text;
            for (auto it = history.rbegin(); it != history.rend(); ++it)
                text += *it + '\n';
            if (activity)
                activity->SetText(text);
            if (historyLabel)
                historyLabel->SetText(text);
            std::cout << "{\"showcase\":" << std::quoted(message) << "}" << std::endl;
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
            if (name == "Controls Gallery" || name == "Menu Gallery")
            {
                auto& target = name == "Controls Gallery" ? *controls.window : menuHost.Window();
                Check(target.SetVisible(true), "Cannot reopen gallery");
                const auto activated = target.RequestActivation();
                if (activated != LWS::Result::Success && activated != LWS::Result::NotSupported)
                    Log("Activation unavailable");
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
        template <class T>
        T& SampleControl(Container& parent, std::string_view type, std::unique_ptr<T> control)
        {
            T* pointer = control.get();
            parent.Add(std::move(control));
            samples.push_back({type, pointer});
            return *pointer;
        }
        void BuildControls()
        {
            auto list = std::make_unique<StackPanel>();
            list->spacing = 12;
            Text(*list, "Every control has a named example. Changes below update the Workspace preview.");
            auto& index = list->Emplace<StackPanel>();
            index.orientation = Orientation::Horizontal;
            const std::array<const char*, 4> names{"Text", "Choices", "Values", "Files / color"};
            for (size_t i = 0; i < names.size(); ++i)
            {
                auto& button = index.Emplace<Button>(names[i]);
                controls.connections.Add(button.OnClick,
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
            auto& label = SampleControl(*list, "Label",
                                        std::make_unique<Label>("Label: resize this gallery to see a long explanation "
                                                                "wrap without creating another native window."));
            label.wrap = true;
            name = &SampleControl(*list, "TextBox", std::make_unique<TextBox>(state.title));
            name->SetPlaceholder("Enter a preview title");
            controls.connections.Add(name->OnEdit,
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
            Text(*list, "Multiline notes: Enter adds a line; Ctrl+Enter commits; Escape restores the draft.");
            notes = &list->Emplace<TextBox>(state.notes, TextBoxMode::Multiline);
            notes->SetVisibleLines(4);
            notes->SetPlaceholder("Add notes about this scene...");
            controls.connections.Add(notes->OnEdit,
                                     [this](const std::string& value, EditPhase phase)
                                     {
                                         state.notes = value;
                                         if (phase != EditPhase::Preview)
                                             Log(phase == EditPhase::Cancel ? "Notes restored" : "Notes committed");
                                     });
            auto& readonly = list->Emplace<TextBox>("Read-only TextBox: select and copy this explanation.");
            readonly.SetReadOnly(true);
            auto& borderless = list->Emplace<TextBox>("Borderless TextBox: this line is editable.");
            borderless.SetBorderless(true);
            auto& empty = list->Emplace<TextBox>();
            empty.SetPlaceholder("An empty TextBox with placeholder text");
            auto& actions = list->Emplace<StackPanel>();
            actions.orientation = Orientation::Horizontal;
            auto& reset = SampleControl(actions, "Button", std::make_unique<Button>("Reset values"));
            reset.focusLossPhase = EditPhase::Cancel;
            controls.connections.Add(reset.OnClick, [this] { Command("Reset values"); });
            auto& flat = actions.Emplace<Button>("Flat: show Tiles");
            flat.flat = true;
            controls.connections.Add(flat.OnClick, [this] { Command("Tiles"); });
            actions.Emplace<Button>("Disabled").SetEnabled(false);
            Section(*list, "02 / Choices",
                    "CheckBox toggles the preview. Standalone RadioButtons are paired by this app; RadioGroup manages "
                    "a set itself.");
            enabled = &SampleControl(*list, "CheckBox", std::make_unique<CheckBox>("Enable preview colors"));
            enabled->SetValue(true);
            controls.connections.Add(enabled->OnChange,
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
            auto& radios = list->Emplace<StackPanel>();
            radios.orientation = Orientation::Horizontal;
            auto& circles = SampleControl(radios, "RadioButton", std::make_unique<RadioButton>("Circles"));
            auto& tiles = radios.Emplace<RadioButton>("Tiles");
            circles.SetValue(true);
            circleRadio = &circles;
            tileRadio = &tiles;
            controls.connections.Add(circles.OnChange,
                                     [this](bool on)
                                     {
                                         if (on)
                                         {
                                             Command("Circles");
                                         }
                                     });
            controls.connections.Add(tiles.OnChange,
                                     [this](bool on)
                                     {
                                         if (on)
                                         {
                                             Command("Tiles");
                                         }
                                     });
            quality = &SampleControl(*list, "RadioGroup",
                                     std::make_unique<RadioGroup>(std::vector<Choice>{{"Fast", "Fast"},
                                                                                      {"Balanced", "Balanced"},
                                                                                      {"High", "High quality"}}));
            quality->orientation = Orientation::Horizontal;
            quality->SetValue(state.quality);
            controls.connections.Add(quality->OnChange,
                                     [this](const std::string& value)
                                     {
                                         state.quality = value;
                                         Refresh();
                                         Log("Quality: " + value);
                                     });
            Text(*list, "ComboBox: select the scene, or open its popup with the keyboard.");
            scene = &SampleControl(*list, "ComboBox",
                                   std::make_unique<ComboBox>(
                                       std::vector<Choice>{{"Circles", "Circles"}, {"Tiles", "Tiles"}}));
            scene->SetValue(state.scene);
            controls.connections.Add(scene->OnChange, [this](const std::string& value) { Command(value); });
            Section(*list, "03 / Numbers, sliders, and scrolling",
                    "NumericEdit: type a value or hold the rocker. Sliders preview while dragging; Escape restores the "
                    "previous value.");
            Text(*list, "NumericEdit<int64_t>: shape count, 1 to 12.");
            count = &SampleControl(*list, "NumericEdit<int64_t>",
                                   std::make_unique<NumericEdit<int64_t>>(NumericSpec<int64_t>{1, 12, 1}));
            count->SetValue(state.count);
            controls.connections.Add(count->OnEdit,
                                     [this](int64_t value, EditPhase)
                                     {
                                         state.count = value;
                                         Refresh();
                                     });
            Text(*list, "NumericEdit<double>: shape scale, 0.5 to 2.0.");
            scale = &SampleControl(*list, "NumericEdit<double>",
                                   std::make_unique<NumericEdit<double>>(NumericSpec<double>{.5, 2, .1}));
            scale->SetValue(state.scale);
            controls.connections.Add(scale->OnEdit,
                                     [this](double value, EditPhase)
                                     {
                                         state.scale = value;
                                         Refresh();
                                     });
            Text(*list, "Slider: outline and filled variants share preview opacity.");
            opacity = &SampleControl(*list, "Slider", std::make_unique<Slider>());
            opacity->SetValue(state.opacity);
            filled = &list->Emplace<Slider>();
            filled->filled = true;
            filled->SetValue(state.opacity);
            controls.connections.Add(opacity->OnEdit,
                                     [this](double value, EditPhase)
                                     {
                                         state.opacity = value;
                                         Refresh();
                                     });
            controls.connections.Add(filled->OnEdit,
                                     [this](double value, EditPhase)
                                     {
                                         state.opacity = value;
                                         Refresh();
                                     });
            Text(*list, "Standalone ScrollBar: move its thumb to inspect different lines in a fixed-height viewport.");
            auto scrollGrid = std::make_unique<Grid>();
            scrollGrid->columns = {-1, 22};
            auto& viewportText = scrollGrid->Emplace<Label>(
                "Sample line 1\nSample line 2\nSample line 3\nSample line 4");
            viewportText.wrap = true;
            auto& scrollbar = SampleControl(*scrollGrid, "ScrollBar", std::make_unique<ScrollBar>());
            scrollbar.SetPage(.2);
            controls.connections.Add(scrollbar.OnEdit,
                                     [&viewportText](double value, EditPhase)
                                     {
                                         const int first = 1 + int(value * 16);
                                         std::string text;
                                         for (int i = first; i < first + 4; ++i)
                                             text += "Sample line " + std::to_string(i) + "\n";
                                         viewportText.SetText(text);
                                     });
            list->Emplace<Viewport>(std::move(scrollGrid), 110);
            Section(*list, "04 / Color and files",
                    "ColorSwatch is a standalone button. ColorPicker previews RGB/HSL edits and restores the color on "
                    "Cancel.");
            auto& sample = SampleControl(*list, "ColorSwatch", std::make_unique<ColorSwatch>());
            sample.SetValue(state.color);
            catalogSwatch = &sample;
            controls.connections.Add(sample.OnClick,
                                     [this]
                                     {
                                         state.color = LLUtils::Color{uint32_t{0xf6a44cff}};
                                         color->SetValue(state.color);
                                         Refresh();
                                         Log("Swatch: amber");
                                     });
            color = &SampleControl(*list, "ColorPicker", std::make_unique<ColorPicker>());
            color->SetValue(state.color);
            controls.connections.Add(color->OnEdit,
                                     [this](LLUtils::Color value, EditPhase)
                                     {
                                         state.color = value;
                                         Refresh();
                                     });
            Text(*list, "FilePicker: edit an existing file path, click Browse, or press F4. The selected file is never "
                        "modified.");
            file = &SampleControl(*list, "FilePicker", std::make_unique<FilePicker>());
            controls.connections.Add(file->OnEdit,
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
            Text(*content, "Horizontal StackPanel: children share the available width.");
            auto& horizontal = content->Emplace<StackPanel>();
            horizontal.orientation = Orientation::Horizontal;
            for (auto name : {"Circles", "Tiles"})
            {
                auto& button = horizontal.Emplace<Button>(name);
                gallery.connections.Add(button.OnClick, [this, name] { Command(name); });
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
            gallery.connections.Add(branch.OnExpanded,
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
            preview.connections.Add(quickEnabled->OnChange,
                                    [this](bool value)
                                    {
                                        state.enabled = value;
                                        Refresh();
                                        Log(state.enabled ? "Quick Controls: enabled" : "Quick Controls: disabled");
                                    });
            Text(*quick, "Opacity");
            quickOpacity = &quick->Emplace<Slider>();
            quickOpacity->filled = true;
            preview.connections.Add(quickOpacity->OnEdit,
                                    [this](double value, EditPhase phase)
                                    {
                                        state.opacity = value;
                                        Refresh();
                                        if (phase != EditPhase::Preview)
                                            Log(phase == EditPhase::Cancel ? "Opacity restored" : "Opacity changed");
                                    });
            auto& reset = quick->Emplace<Button>("Reset values");
            reset.focusLossPhase = EditPhase::Cancel;
            preview.connections.Add(reset.OnClick, [this] { Command("Reset values"); });
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
            SetPaneRoot(preview, std::move(panes), "Live preview / one native child");
            auto fixedBody = std::make_unique<Grid>();
            fixedBody->columns = {60, -1};
            swatch = &fixedBody->Emplace<ColorSwatch>();
            swatch->SetValue(state.color);
            fixed.connections.Add(swatch->OnClick, [this] { Command("Controls Gallery"); });
            auto& button = fixedBody->Emplace<Button>("Show Controls");
            fixed.connections.Add(button.OnClick, [this] { Command("Controls Gallery"); });
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
                menuConnections.Add(button.OnClick, [this, name] { Command(name); });
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
            for (auto* surface : {&main, &controls, &gallery, &preview, &fixed})
                surface->host->Update();
            Save(Render(*controls.host), "controls.ppm");
            Save(Render(menuHost), "menu-gallery.ppm");
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
        }
    };
    Showcase::Showcase(LWS::PlatformContext& platform, UIHost& host)
        : impl_(std::make_unique<Impl>(*this, platform, host))
    {
        impl_->samples.push_back({"MenuBar", impl_->main.host->MainMenu()});
    }
    Showcase::~Showcase() = default;
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
        bool result = !impl_->failed && Samples().size() == 23;
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
        std::cout << "Showcase verification: " << Samples().size() << " catalog entries, "
                  << (result ? "passed" : "FAILED") << std::endl;
        return result;
    }
}  // namespace LWSUI::demo
