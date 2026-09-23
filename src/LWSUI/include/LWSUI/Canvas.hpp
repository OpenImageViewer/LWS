#pragma once
#include <LWS/Bitmap.hpp>
#include <LWS/WindowTypes.hpp>
#include <LWSUI/TextStyle.hpp>
#include <span>
#include <cmath>
#include <memory>
#include <string_view>
#include <vector>
namespace LWSUI
{
    class Canvas
    {
      public:

        Canvas();
        ~Canvas();
        void Begin(int width, int height, LWS::ContentScale scale = {});
        LWS::ContentScale Scale() const { return scale_; }
        void Fill(float x, float y, float width, float height, LLUtils::Color color);
        void Text(std::string_view text, float x, float y, float width, float height, LLUtils::Color color,
                  const FontSpec& font = {}, bool wrap = false, std::span<const TextSpan> spans = {});
        void CenteredText(std::string_view text, float x, float y, float width, float height, LLUtils::Color color,
                          const FontSpec& font = {}, std::span<const TextSpan> spans = {});
        void Line(float x1, float y1, float x2, float y2, float thickness, LLUtils::Color color);
        // Logical center/radius; strokes are centered on the circumference.
        void FillCircle(float centerX, float centerY, float radius, LLUtils::Color color);
        void StrokeCircle(float centerX, float centerY, float radius, float thickness, LLUtils::Color color);
        void Image(const LWS::BitmapBuffer& image, float x, float y, float width, float height);
        float Measure(std::string_view text, const FontSpec& font = {});
        // Returns a UTF-8 insertion offset using native direction/cluster hit testing.
        size_t HitTestText(std::string_view text, float x, const FontSpec& font = {});
        float Caret(std::string_view text, size_t byteOffset, const FontSpec& font = {});
        float TextHeight(std::string_view text, float width, const FontSpec& font = {}, bool wrap = true);
        void Clip(float x, float y, float width, float height);
        void Unclip();
        class ClipScope
        {
          public:

            ClipScope(Canvas& canvas, float x, float y, float w, float h) : canvas_(canvas)
            {
                const auto scale = canvas_.Scale();
                // Conservative pixel bounds preserve aliased clip edges at fractional DPI.
                left_ = std::floor(double(x) * scale.x);
                top_ = std::floor(double(y) * scale.y);
                right_ = std::ceil((double(x) + std::max(0.f, w)) * scale.x);
                bottom_ = std::ceil((double(y) + std::max(0.f, h)) * scale.y);
                previous_ = canvas_.clip_;
                if (previous_)
                {
                    left_ = std::max(left_, previous_->left_);
                    top_ = std::max(top_, previous_->top_);
                    right_ = std::min(right_, previous_->right_);
                    bottom_ = std::min(bottom_, previous_->bottom_);
                }
                canvas_.Clip(x, y, w, h);
                canvas_.clip_ = this;
            }
            ~ClipScope()
            {
                canvas_.clip_ = previous_;
                canvas_.Unclip();
            }
            ClipScope(const ClipScope&) = delete;

          private:

            friend class Canvas;
            Canvas& canvas_;
            const ClipScope* previous_ = nullptr;
            double left_, top_, right_, bottom_;
        };
        LWS::BitmapBuffer End();

      private:

        friend class Control;
        bool OutsideClip(float x, float y, float width, float height) const
        {
            return clip_ && (clip_->left_ >= clip_->right_ || clip_->top_ >= clip_->bottom_ ||
                             std::ceil((double(x) + width) * scale_.x) <= clip_->left_ ||
                             std::ceil((double(y) + height) * scale_.y) <= clip_->top_ ||
                             std::floor(double(x) * scale_.x) >= clip_->right_ ||
                             std::floor(double(y) * scale_.y) >= clip_->bottom_);
        }
        // Existing RAII scopes form the clip chain; no heap-backed clip stack.
        const ClipScope* clip_ = nullptr;
        void PaintText(std::string_view text, float x, float y, float width, float height, LLUtils::Color color,
                       const FontSpec& font, bool wrap, std::span<const TextSpan> spans, bool centered);
        struct Impl;
        std::unique_ptr<Impl> impl_;
        LWS::ContentScale scale_;
    };
}  // namespace LWSUI
