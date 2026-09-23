#pragma once
#include <LWSUI/Composites.hpp>
#include <LWSUI/UIHost.hpp>
#include <array>
#include <cstdio>

namespace LWSUI::internal
{
    inline std::string ColorText(LLUtils::Color color)
    {
        char text[10];
        std::snprintf(text, sizeof(text), "#%02x%02x%02x%02x", color.R(), color.G(), color.B(), color.A());
        return text;
    }
    inline std::optional<LLUtils::Color> ParseColor(std::string_view text)
    {
        if ((text.size() != 7 && text.size() != 9) || text.front() != '#')
            return {};
        uint32_t value = 0;
        const auto [end, error] = std::from_chars(text.data() + 1, text.data() + text.size(), value, 16);
        if (error != std::errc{} || end != text.data() + text.size())
            return {};
        return LLUtils::Color{text.size() == 7 ? (value << 8) | 255 : value};
    }

    // CPU raster caches are local to the popup. Marker movement never regenerates a gradient.
    class ColorGradient
    {
      public:

        void Draw(Canvas& canvas, const Control& control, bool spectrum, double hue)
        {
            const auto b = control.Bounds();
            if (b.width <= 0 || b.height <= 0)
                return;
            auto scale = control.Host() ? control.Host()->Window().GetClientAreaMetrics().Scale()
                                        : std::optional<LWS::ContentScale>{};
            const int width = spectrum ? 1 : int(std::clamp(std::ceil(b.width * (scale ? scale->x : 1)), 2., 512.));
            const int height = int(std::clamp(std::ceil(b.height * (scale ? scale->y : 1)), 2., 512.));
            if (width != width_ || height != height_ || (!spectrum && hue != hue_))
            {
                std::vector<std::byte> pixels(size_t(width) * height * 4);
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x)
                    {
                        const auto color = HslToRgb(spectrum ? HslColor{360. * y / (height - 1), 1, .5}
                                                             : HslColor{hue, double(x) / (width - 1),
                                                                        1. - double(y) / (height - 1)},
                                                    255);
                        auto* pixel = pixels.data() + (size_t(y) * width + x) * 4;
                        pixel[0] = std::byte{color.B()};
                        pixel[1] = std::byte{color.G()};
                        pixel[2] = std::byte{color.R()};
                        pixel[3] = std::byte{255};
                    }
                pixels_ = std::move(pixels);
                width_ = width;
                height_ = height;
                hue_ = hue;
            }
            canvas.Image({pixels_, LWS::BitmapPixelFormat::Bgra8Premultiplied, LWS::BitmapRowOrder::TopDown,
                          uint32_t(width_), uint32_t(height_), uint32_t(width_ * 4)},
                         b.x, b.y, b.width, b.height);
        }

      private:

        std::vector<std::byte> pixels_;
        int width_ = 0, height_ = 0;
        double hue_ = -1;
    };

    class HueSlider final : public Slider
    {
      public:

        HueSlider() { keyboardStep = 1. / 360; }

      protected:

        double Fraction(float, float y) const override
        {
            return std::clamp(double((y - Bounds().y) / std::max(1.f, Bounds().height - 1)), 0., 1.);
        }
        bool OnInput(const Input& input) override
        {
            auto mapped = input;
            if (mapped.key == LWS::KeyCode::Up)
                mapped.key = LWS::KeyCode::Left;
            if (mapped.key == LWS::KeyCode::Down)
                mapped.key = LWS::KeyCode::Right;
            return Slider::OnInput(mapped);
        }
        void OnRender(Canvas& canvas) override
        {
            gradient_.Draw(canvas, *this, true, 0);
            const auto b = Bounds();
            const float y = b.y + float(Value()) * std::max(0.f, b.height - 1);
            canvas.Line(b.x, y, b.x + b.width, y, 4, LLUtils::Color{uint32_t{0xffffffff}});
            canvas.Line(b.x, y, b.x + b.width, y, 2, LLUtils::Color{uint32_t{0x000000ff}});
        }

      private:

        ColorGradient gradient_;
    };

    class SaturationLightness final : public Control
    {
      public:

        explicit SaturationLightness(std::function<void(double, double)> changed) : changed_(std::move(changed)) {}
        bool Focusable() const override { return true; }
        void SetValue(HslColor value)
        {
            value_ = value;
            Invalidate();
        }
        bool Finish(EditPhase phase) override
        {
            if (dragging_)
            {
                dragging_ = false;
                ReleaseCapture();
                if (phase == EditPhase::Cancel)
                    Change(start_.saturation, start_.lightness);
            }
            return true;
        }

      protected:

        bool OnInput(const Input& input) override
        {
            if (input.kind == InputKind::Down)
            {
                start_ = value_;
                dragging_ = true;
                Capture();
            }
            if ((input.kind == InputKind::Down || input.kind == InputKind::Move) && dragging_)
            {
                const auto b = Bounds();
                Change((input.x - b.x) / std::max(1.f, b.width - 1), 1 - (input.y - b.y) / std::max(1.f, b.height - 1));
                return true;
            }
            if (input.kind == InputKind::Up)
                return Finish(EditPhase::Commit);
            if (input.kind == InputKind::Cancel)
                return Finish(EditPhase::Cancel);
            if (input.kind == InputKind::KeyDown)
            {
                const double step = input.shift ? .1 : .01;
                auto s = value_.saturation, l = value_.lightness;
                switch (input.key)
                {
                    case LWS::KeyCode::Left:
                        s -= step;
                        break;
                    case LWS::KeyCode::Right:
                        s += step;
                        break;
                    case LWS::KeyCode::Up:
                        l += step;
                        break;
                    case LWS::KeyCode::Down:
                        l -= step;
                        break;
                    default:
                        return false;
                }
                Change(s, l);
                return true;
            }
            return false;
        }
        void OnRender(Canvas& canvas) override
        {
            gradient_.Draw(canvas, *this, false, value_.hue);
            const auto b = Bounds();
            const float x = b.x + float(value_.saturation) * std::max(0.f, b.width - 1);
            const float y = b.y + float(1 - value_.lightness) * std::max(0.f, b.height - 1);
            for (auto [thickness, color] : {std::pair{4.f, uint32_t{0xffffffff}}, std::pair{2.f, uint32_t{0x000000ff}}})
            {
                canvas.Line(x - 6, y, x + 6, y, thickness, LLUtils::Color{color});
                canvas.Line(x, y - 6, x, y + 6, thickness, LLUtils::Color{color});
            }
        }

      private:

        void Change(double saturation, double lightness)
        {
            value_.saturation = std::clamp(saturation, 0., 1.);
            value_.lightness = std::clamp(lightness, 0., 1.);
            changed_(value_.saturation, value_.lightness);
            Invalidate();
        }
        HslColor value_, start_;
        bool dragging_ = false;
        std::function<void(double, double)> changed_;
        ColorGradient gradient_;
    };

    class ColorComparison final : public Control
    {
      public:

        explicit ColorComparison(LLUtils::Color original) : original_(original), current_(original) {}
        void SetValue(LLUtils::Color current)
        {
            current_ = current;
            Invalidate();
        }
        LLUtils::Color Original() const { return original_; }

      protected:

        void OnRender(Canvas& canvas) override
        {
            const auto b = Bounds();
            if (b.width <= 0 || b.height <= 0)
                return;
            const auto scale = canvas.Scale();
            const Key key{int(std::clamp(std::ceil(b.width * scale.x), 1., 4096.)),
                          int(std::clamp(std::ceil(b.height * scale.y), 2., 4096.)),
                          scale.x,
                          scale.y,
                          Style().checkerSize,
                          current_,
                          Style().checkerDark,
                          Style().checkerLight};
            if (!cached_ || *cached_ != key)
            {
                std::vector<std::byte> pixels(size_t(key.width) * key.height * 4);
                const int cellX = int(std::clamp(std::round(key.checkerSize * scale.x), 1., 4096.));
                const int cellY = int(std::clamp(std::round(key.checkerSize * scale.y), 1., 4096.));
                for (int half = 0; half < 2; ++half)
                {
                    const auto color = half == 0 ? current_ : original_;
                    const bool opaque = color.A() == 255;
                    const auto dark = opaque ? Pixel{std::byte{color.B()}, std::byte{color.G()}, std::byte{color.R()},
                                                     std::byte{255}}
                                             : Over(color, key.dark);
                    const auto light = opaque ? dark : Over(color, key.light);
                    const int first = half == 0 ? 0 : key.height / 2;
                    const int last = half == 0 ? key.height / 2 : key.height;
                    for (int y = first; y < last; ++y)
                        for (int x = 0; x < key.width; ++x)
                        {
                            // Opaque halves never evaluate the checkerboard or blend colors.
                            const auto& pixel = opaque || ((x / cellX + y / cellY) % 2) ? dark : light;
                            std::copy(pixel.begin(), pixel.end(), pixels.begin() + (size_t(y) * key.width + x) * 4);
                        }
                }
                pixels_ = std::move(pixels);
                cached_ = key;
            }
            canvas.Image({pixels_, LWS::BitmapPixelFormat::Bgra8Premultiplied, LWS::BitmapRowOrder::TopDown,
                          uint32_t(key.width), uint32_t(key.height), uint32_t(key.width * 4)},
                         b.x, b.y, b.width, b.height);
        }

      private:

        using Pixel = std::array<std::byte, 4>;
        static Pixel Over(LLUtils::Color foreground, LLUtils::Color background)
        {
            const unsigned a = foreground.A(), back = background.A(), remaining = 255 - a;
            const auto channel = [&](unsigned f, unsigned b)
            { return std::byte((f * a * 255 + b * back * remaining + 32512) / 65025); };
            return {channel(foreground.B(), background.B()), channel(foreground.G(), background.G()),
                    channel(foreground.R(), background.R()), std::byte(a + (back * remaining + 127) / 255)};
        }
        struct Key
        {
            int width, height;
            double scaleX, scaleY;
            float checkerSize;
            LLUtils::Color current, dark, light;
            bool operator==(const Key&) const = default;
        };
        LLUtils::Color original_, current_;
        std::optional<Key> cached_;
        std::vector<std::byte> pixels_;
    };

    class ColorPanel final : public Container
    {
      public:

        ColorPanel(LLUtils::Color color, std::function<void(LLUtils::Color)> preview, const Theme& style = {})
            : color_(color), hsl_(RgbToHsl(color)), preview_(std::move(preview))
        {
            title_ = &Emplace<Label>("Color");
            title_->fontRole = FontRole::Title;
            plane_ = &Emplace<SaturationLightness>(
                [this](double s, double l)
                {
                    hsl_.saturation = s;
                    hsl_.lightness = l;
                    color_ = HslToRgb(hsl_, color_.A());
                    Refresh();
                    preview_(color_);
                });
            hue_ = &Emplace<HueSlider>();
            hueEdit_ = hue_->OnEdit.Connect([this](double value, EditPhase) { SetChannel(4, value); });
            planeLabel_ = &Emplace<Label>("Saturation →   Lightness ↑");
            planeLabel_->fontRole = FontRole::Small;
            hueLabel_ = &Emplace<Label>("H");
            hueLabel_->fontRole = FontRole::Small;
            const std::array names{"R", "G", "B", "A", "H", "S", "L"};
            for (size_t i = 0; i < names.size(); ++i)
            {
                labels_[i] = &Emplace<Label>(names[i]);
                sliders_[i] = &Emplace<Slider>();
                sliders_[i]->filled = true;
                sliders_[i]->keyboardStep = 1. / Maximum(i);
                values_[i] = &Emplace<TextBox>();
                values_[i]->fontRole = FontRole::Small;
                units_[i] = &Emplace<Label>(i == 4 ? "°" : i > 4 ? "%" : "");
                units_[i]->fontRole = FontRole::Small;
                sliderEdits_[i] = sliders_[i]->OnEdit.Connect([this, i](double value, EditPhase)
                                                              { SetChannel(i, value); });
                valueEdits_[i] = values_[i]->OnEdit.Connect(
                    [this, i](const std::string& text, EditPhase phase)
                    {
                        double value = 0;
                        auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value,
                                                            std::chars_format::fixed);
                        if (text.empty() || error != std::errc{} || end != text.data() + text.size() ||
                            !std::isfinite(value) || value < 0 || value > Maximum(i) ||
                            (i < 4 && text.find_first_not_of("0123456789") != text.npos))
                        {
                            Invalid(*values_[i], i < 4    ? "Enter an integer from 0 to 255"
                                                 : i == 4 ? "Enter a hue from 0 to 360"
                                                          : "Enter a percentage from 0 to 100");
                            return;
                        }
                        SetChannel(i, value / Maximum(i), phase == EditPhase::Preview ? values_[i] : nullptr);
                    });
            }
            originalLabel_ = &Emplace<Label>("Original");
            newLabel_ = &Emplace<Label>("New");
            originalLabel_->fontRole = newLabel_->fontRole = FontRole::Small;
            hex_ = &Emplace<TextBox>();
            hex_->SetPlaceholder("#RRGGBBAA");
            hexEdit_ = hex_->OnEdit.Connect(
                [this](const std::string& text, EditPhase phase)
                {
                    auto parsed = ParseColor(text);
                    if (!parsed)
                    {
                        Invalid(*hex_, "Use #RRGGBB or #RRGGBBAA");
                        return;
                    }
                    color_ = *parsed;
                    hsl_ = RgbToHsl(color_, hsl_.hue);
                    Refresh(phase == EditPhase::Preview ? hex_ : nullptr);
                    preview_(color_);
                });
            originalHex_ = &Emplace<TextBox>(ColorText(color));
            originalHex_->fontRole = FontRole::Small;
            originalHex_->SetReadOnly(true);
            originalHex_->SetBorderless(true);
            comparison_ = &Emplace<ColorComparison>(color);
            hint_ = &Emplace<Label>();
            hint_->fontRole = FontRole::Small;
            hint_->wrap = true;
            cancel_ = &Emplace<Button>("Cancel");
            accept_ = &Emplace<Button>("Accept");
            cancel_->flat = accept_->flat = true;
            cancel_->focusLossPhase = EditPhase::Cancel;
            cancel_->SetHighlightPattern("");
            accept_->SetHighlightPattern("");
            cancelClick_ = cancel_->OnClick.Connect(
                [this]
                {
                    if (Host())
                        Host()->ClosePopup(EditPhase::Cancel);
                });
            acceptClick_ = accept_->OnClick.Connect(
                [this]
                {
                    if (Host())
                        Host()->ClosePopup(EditPhase::Commit);
                });
            Configure(style);
            Refresh();
        }
        void Configure(const Theme& style)
        {
            SetStyle(style);
            auto compact = style;
            compact.rowHeight = 26;
            compact.textPadding = std::min(style.textPadding, 3.f);
            compact.labelPadding = std::min(style.labelPadding, 2.f);
            for (auto* label : labels_)
                label->SetStyle(compact);
            for (auto* unit : units_)
                unit->SetStyle(compact);
            for (auto* value : values_)
                value->SetStyle(compact);
            hex_->SetStyle(compact);
            originalHex_->SetStyle(compact);
        }
        std::optional<Size> Fit(UIHost& host, Size available)
        {
            const auto m = Metrics(available.width, std::min(available.height, Style().popupHeight), &host);
            if (m.channelWidth < m.minimumChannel || m.height > available.height)
                return {};
            return Size{available.width, m.height};
        }
        Control& InitialFocus() { return *plane_; }

      protected:

        Size OnMeasure(Size available) override
        {
            for (auto& child : Children())
                child->Measure(available);
            const auto m = Metrics(available.width, std::min(available.height, Style().popupHeight));
            return {available.width, m.height};
        }
        void OnArrange() override
        {
            const auto b = Bounds();
            const auto m = Metrics(b.width, b.height);
            const auto& s = Style();
            auto place = [&](Control& c, float x, float y, float w, float h)
            { c.Arrange({b.x + x, b.y + y, std::max(0.f, w), std::max(0.f, h)}); };
            place(*title_, m.padding, m.padding, m.inner, m.header);
            const float top = m.padding + m.header + m.gap;
            const float plotTop = top + m.label + m.rowGap;
            place(*planeLabel_, m.padding, top, m.planeWidth, m.label);
            place(*hueLabel_, m.padding + m.planeWidth + m.gap, top, m.hueWidth, m.label);
            place(*plane_, m.padding, plotTop, m.planeWidth, m.planeHeight);
            place(*hue_, m.padding + m.planeWidth + m.gap, plotTop, m.hueWidth, m.planeHeight);
            const float right = m.stacked ? m.padding : b.width - m.padding - m.channelWidth;
            const float channelTop = m.stacked ? plotTop + m.planeHeight + m.gap : top;
            const float nameWidth = s.font.size * 1.3f, unitWidth = s.smallFont.size * 1.5f;
            const float valueWidth = std::min(
                m.channelWidth * .35f, std::max(64.f, TextWidth("360.00", values_[4]->Font()) + 2 * s.textPadding));
            const float valueX = right + m.channelWidth - valueWidth - unitWidth;
            for (size_t i = 0; i < sliders_.size(); ++i)
            {
                const float y = channelTop + i * (m.row + m.rowGap);
                place(*labels_[i], right, y, nameWidth, m.row);
                place(*sliders_[i], right + nameWidth, y, valueX - right - nameWidth - m.rowGap, m.row);
                place(*values_[i], valueX, y, valueWidth, m.row);
                place(*units_[i], valueX + valueWidth, y, unitWidth, m.row);
            }
            const float previewX = right + (m.channelWidth - 2 * m.previewWidth) / 2;
            const float previewY = channelTop + m.channelsHeight + m.gap;
            const float labelWidth = std::min(m.channelWidth,
                                              TextWidth("Original", originalLabel_->Font()) + 2 * s.labelPadding);
            const float hexX = right + labelWidth + m.rowGap;
            const float hexWidth = std::max(0.f, m.channelWidth - labelWidth - m.rowGap);
            place(*newLabel_, right, previewY + (m.previewRow - m.label) / 2, labelWidth, m.label);
            place(*hex_, hexX, previewY, hexWidth, m.previewRow);
            place(*comparison_, previewX, previewY + m.previewRow, 2 * m.previewWidth, m.previewHeight);
            const float originalY = previewY + m.previewRow + m.previewHeight;
            place(*originalLabel_, right, originalY + (m.previewRow - m.label) / 2, labelWidth, m.label);
            place(*originalHex_, hexX, originalY, hexWidth, m.previewRow);
            float y = top + m.bodyHeight + m.gap;
            hint_->SetForeground(error_.empty() ? s.muted : s.error);
            cancel_->SetForeground(s.muted);
            accept_->SetForeground(s.accent);
            place(*hint_, m.padding, y, m.inner, m.hint);
            y += m.hint + m.gap;
            const float buttonWidth = std::min(std::max(s.popupButtonWidth, s.font.size * 6),
                                               std::max(0.f, (m.inner - m.gap) / 2));
            place(*cancel_, b.width - m.padding - 2 * buttonWidth - m.gap, y, buttonWidth, m.button);
            place(*accept_, b.width - m.padding - buttonWidth, y, buttonWidth, m.button);
        }
        void OnRender(Canvas& canvas) override
        {
            const auto b = Bounds();
            canvas.Fill(b.x, b.y, b.width, b.height, Style().background);
            Container::OnRender(canvas);
        }
        bool OnPreviewInput(const Input& input) override
        {
            if (RestoreOriginal(input))
                return true;
            if (input.kind != InputKind::KeyDown || (input.control && input.alt))
                return false;
            const bool arrow = input.key == LWS::KeyCode::Up || input.key == LWS::KeyCode::Down ||
                               input.key == LWS::KeyCode::Left || input.key == LWS::KeyCode::Right;
            if (input.control && input.key == LWS::KeyCode::Enter)
            {
                if (Host())
                    Host()->ClosePopup(EditPhase::Commit);
                return true;
            }
            const bool vertical = input.control && (input.key == LWS::KeyCode::Up || input.key == LWS::KeyCode::Down);
            if (!vertical && !(input.alt && arrow))
                return false;
            if (!Finish(EditPhase::Commit))
                return true;
            auto mapped = input;
            mapped.control = mapped.alt = false;
            if (vertical)
                hue_->Dispatch(mapped);
            else
                plane_->Dispatch(mapped);
            return true;
        }
        bool OnInput(const Input& input) override
        {
            if (input.kind == InputKind::KeyDown && input.key == LWS::KeyCode::Enter && Host())
            {
                Host()->ClosePopup();
                return true;
            }
            return false;
        }

      private:

        struct Click
        {
            float x, y;
            std::chrono::steady_clock::time_point time;
        };
        bool RestoreOriginal(const Input& input)
        {
            const auto b = comparison_->Bounds();
            const bool inside = b.Contains(input.x, input.y) && input.y >= b.y + b.height / 2;
            const auto closeEnough = [&](const Click& click)
            { return (input.x - click.x) * (input.x - click.x) + (input.y - click.y) * (input.y - click.y) <= 25; };
            if (input.kind == InputKind::Cancel)
            {
                pressedClick_.reset();
                previousClick_.reset();
            }
            if (input.kind == InputKind::Down)
            {
                pressedClick_.reset();
                if (inside)
                    pressedClick_ = Click{input.x, input.y, std::chrono::steady_clock::now()};
                else
                    previousClick_.reset();
            }
            if (input.kind == InputKind::Move && (!inside || (pressedClick_ && !closeEnough(*pressedClick_))))
            {
                pressedClick_.reset();
                previousClick_.reset();
            }
            if (input.kind != InputKind::Up)
                return false;
            const auto now = std::chrono::steady_clock::now();
            const bool click = pressedClick_ && inside && closeEnough(*pressedClick_) &&
                               now - pressedClick_->time <= std::chrono::milliseconds(500);
            pressedClick_.reset();
            if (!click)
            {
                previousClick_.reset();
                return false;
            }
            if (previousClick_ && closeEnough(*previousClick_) &&
                now - previousClick_->time <= std::chrono::milliseconds(500))
            {
                previousClick_.reset();
                color_ = comparison_->Original();
                hsl_ = RgbToHsl(color_);
                Refresh();  // Silent setters replace invalid drafts without intermediate edits.
                preview_(color_);
                return true;
            }
            previousClick_ = Click{input.x, input.y, now};
            return false;
        }
        struct Layout
        {
            float padding, inner, gap, rowGap, header, label, row, channelWidth, minimumChannel, hueWidth, planeWidth,
                planeHeight, channelsHeight, previewWidth, previewHeight, previewRow, bodyHeight, hint, button, height;
            bool stacked;
        };
        float TextWidth(std::string_view text, const FontSpec& font, UIHost* measureHost = nullptr) const
        {
            auto* host = measureHost ? measureHost : Host();
            return host ? host->MeasureText(text, 10000, false, font).width : float(text.size()) * font.size * .6f;
        }
        Layout Metrics(float width, float availableHeight, UIHost* measureHost = nullptr) const
        {
            const auto& s = Style();
            Layout m{};
            auto* host = measureHost ? measureHost : Host();
            const auto line = [&](const FontSpec& font)
            { return host ? host->MeasureText("Mg", 10000, false, font).height : font.size * 1.4f; };
            m.padding = std::min(s.popupPadding, std::max(0.f, width / 4));
            m.inner = std::max(0.f, width - 2 * m.padding);
            m.gap = std::min(s.popupGap, m.inner / 4);
            m.rowGap = std::min(3.f, s.labelPadding);
            m.header = std::max(s.rowHeight, line(title_->Font()) + 2 * s.labelPadding);
            m.label = line(s.smallFont) + 2 * s.labelPadding;
            m.row = std::max({26.f, line(values_[0]->Font()) + 2 * values_[0]->Style().textPadding,
                              line(labels_[0]->Font()) + 2 * labels_[0]->Style().labelPadding,
                              line(units_[0]->Font()) + 2 * units_[0]->Style().labelPadding});
            m.button = std::max(s.rowHeight, line(cancel_->Font()) + 2 * s.textPadding);
            m.minimumChannel = std::max(240.f, s.font.size * 6.3f + s.smallFont.size * 1.5f + 64 + m.rowGap);
            m.channelWidth = std::min(m.inner, m.minimumChannel);
            m.hueWidth = std::min(std::max(24.f, s.popupChannelHeight), m.inner / 4);
            m.stacked = m.inner < 192 + m.hueWidth + 2 * m.gap + m.channelWidth;
            if (m.stacked)
                m.channelWidth = m.inner;
            m.planeWidth = std::max(0.f, m.inner - m.hueWidth - m.gap - (m.stacked ? 0 : m.channelWidth + m.gap));
            m.channelsHeight = 7 * m.row + 6 * m.rowGap;
            m.previewWidth = std::min(m.channelWidth / 2, std::max(s.popupPreviewHeight,
                                                                   TextWidth("Original", originalLabel_->Font(), host) +
                                                                       2 * s.labelPadding));
            m.previewRow = std::max({m.label, line(hex_->Font()) + 2 * hex_->Style().textPadding,
                                     line(originalHex_->Font()) + 2 * originalHex_->Style().textPadding});
            m.previewHeight = 2 * m.previewWidth;
            m.hint = host ? host->MeasureText(hint_->Text(), std::max(1.f, m.inner - 2 * s.labelPadding), true,
                                              hint_->Font())
                                    .height +
                                2 * s.labelPadding
                          : hint_->Font().size * 1.4f + 2 * s.labelPadding;
            const float fixed = 2 * m.padding + m.header + m.hint + m.button + 3 * m.gap;
            const float channelBody = m.channelsHeight + m.gap + 2 * m.previewRow;
            m.planeHeight = m.stacked ? std::min(m.planeWidth, m.channelsHeight) : 0;
            const float plotHeader = m.stacked ? m.label + m.rowGap + m.gap : 0;
            float excess = std::max(0.f, fixed + channelBody + m.previewHeight + plotHeader + m.planeHeight -
                                             availableHeight);
            const float reducePreview = std::min(excess, std::max(0.f, m.previewHeight - 32));
            m.previewHeight -= reducePreview;
            excess -= reducePreview;
            if (m.stacked)
                m.planeHeight -= std::min(excess, std::max(0.f, m.planeHeight - 64));
            else
                m.planeHeight = std::max(64.f, channelBody + m.previewHeight - m.label - m.rowGap);
            m.bodyHeight = m.stacked ? channelBody + m.previewHeight + plotHeader + m.planeHeight
                                     : std::max(channelBody + m.previewHeight, m.label + m.rowGap + m.planeHeight);
            m.height = fixed + m.bodyHeight;
            return m;
        }
        static double Maximum(size_t channel) { return channel < 4 ? 255 : channel == 4 ? 360 : 100; }
        void Invalid(TextBox& field, const char* message)
        {
            field.SetValidation(message);
            error_ = message;
            hint_->SetText(error_);
            Invalidate(true);
        }
        void SetChannel(size_t channel, double value, TextBox* editing = nullptr)
        {
            if (channel < 4)
            {
                color_.channels[channel] = uint8_t(std::lround(value * 255));
                if (channel != 3)
                    hsl_ = RgbToHsl(color_, hsl_.hue);
            }
            else
            {
                if (channel == 4)
                    hsl_.hue = value * 360;
                else if (channel == 5)
                    hsl_.saturation = value;
                else
                    hsl_.lightness = value;
                color_ = HslToRgb(hsl_, color_.A());
            }
            Refresh(editing);
            preview_(color_);
        }
        void Refresh(TextBox* editing = nullptr)
        {
            error_.clear();
            hint_->SetText("Ctrl+↑/↓: vertical slider · Alt+arrows: color area · Shift: larger steps · Ctrl+Enter: "
                           "accept · Esc: cancel");
            plane_->SetValue(hsl_);
            hue_->SetValue(hsl_.hue / 360);
            comparison_->SetValue(color_);
            const auto hex = ColorText(color_);
            if (hex_ != editing && hex_->Text() != hex)
                hex_->SetText(hex);
            const std::array values{double(color_.R()), double(color_.G()),    double(color_.B()),  double(color_.A()),
                                    hsl_.hue,           hsl_.saturation * 100, hsl_.lightness * 100};
            for (size_t i = 0; i < values.size(); ++i)
            {
                sliders_[i]->SetValue(values[i] / Maximum(i));
                char buffer[40];
                auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), values[i], std::chars_format::fixed,
                                                  i < 4 ? 0 : 2);
                std::string text = error == std::errc{} ? std::string(buffer, end) : "";
                if (i >= 4)
                {
                    while (text.ends_with('0'))
                        text.pop_back();
                    if (text.ends_with('.'))
                        text.pop_back();
                }
                if (values_[i] != editing && values_[i]->Text() != text)
                    values_[i]->SetText(std::move(text));
            }
            Invalidate(true);
        }
        LLUtils::Color color_;
        HslColor hsl_;
        std::function<void(LLUtils::Color)> preview_;
        Label *title_, *planeLabel_, *hueLabel_, *originalLabel_, *newLabel_, *hint_;
        TextBox *hex_, *originalHex_;
        SaturationLightness* plane_;
        HueSlider* hue_;
        ColorComparison* comparison_;
        Button *cancel_, *accept_;
        std::array<Label*, 7> labels_{}, units_{};
        std::array<Slider*, 7> sliders_{};
        std::array<TextBox*, 7> values_{};
        std::string error_;
        std::optional<Click> previousClick_, pressedClick_;
        Event<void(const std::string&, EditPhase)>::Connection hexEdit_;
        Event<void(double, EditPhase)>::Connection hueEdit_;
        std::array<Event<void(double, EditPhase)>::Connection, 7> sliderEdits_;
        std::array<Event<void(const std::string&, EditPhase)>::Connection, 7> valueEdits_;
        Event<void()>::Connection cancelClick_, acceptClick_;
    };
}  // namespace LWSUI::internal
