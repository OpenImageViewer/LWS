#include <LWSUI/Canvas.hpp>
#include <LWSUI/TextEditing.hpp>
#include <LWS/Bitmap.hpp>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <map>
#include <tuple>
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace LWSUI
{
    using Microsoft::WRL::ComPtr;
    static void Check(HRESULT hr)
    {
        if (FAILED(hr))
            throw std::runtime_error("UI drawing failed (HRESULT " + std::to_string(static_cast<uint32_t>(hr)) + ")");
    }
    static std::wstring Wide(std::string_view text)
    {
        if (text.empty())
            return {};
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
        std::wstring s(n, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), s.data(), n);
        return s;
    }
    struct Canvas::Impl
    {
        int width = 0, height = 0;
        ComPtr<ID2D1Factory> factory;
        ComPtr<IDWriteFactory> fonts;
        ComPtr<IWICImagingFactory> imaging;
        ComPtr<IWICBitmap> bitmap;
        ComPtr<ID2D1RenderTarget> target;
        ComPtr<IWICBitmapLock> lock;
        bool com = false;
        std::map<std::tuple<std::string, float, FontSpec, bool>, ComPtr<IDWriteTextLayout>> layouts;
        Impl()
        {
            auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            com = SUCCEEDED(hr);
            if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
                Check(hr);
            Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()));
            Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                      reinterpret_cast<IUnknown**>(fonts.GetAddressOf())));
            Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&imaging)));
        }
        ~Impl()
        {
            layouts.clear();
            lock.Reset();
            target.Reset();
            bitmap.Reset();
            imaging.Reset();
            fonts.Reset();
            factory.Reset();
            if (com)
                CoUninitialize();
        }
        ComPtr<IDWriteTextLayout> Layout(std::string_view text, float width, float height, const FontSpec& font,
                                         bool wrap)
        {
            auto key = std::make_tuple(std::string(text), wrap ? width : 100000.f, font, wrap);
            if (auto found = layouts.find(key); found != layouts.end())
                return found->second;
            // Bound memory with simple eviction. An LRU needs extra ownership and
            // recency bookkeeping; defer it until measured cache churn warrants it.
            if (layouts.size() > 512)
                layouts.clear();
            width = wrap ? width : 100000.f;
            height = 100000;
            ComPtr<IDWriteTextFormat> format;
            auto family = font.family.empty() ? std::wstring(L"Segoe UI") : Wide(font.family);
            ComPtr<IDWriteFontCollection> collection;
            Check(fonts->GetSystemFontCollection(&collection));
            UINT32 familyIndex = 0;
            BOOL exists = FALSE;
            Check(collection->FindFamilyName(family.c_str(), &familyIndex, &exists));
            if (!exists)
                family = L"Segoe UI";
            Check(fonts->CreateTextFormat(family.c_str(), nullptr, DWRITE_FONT_WEIGHT(font.weight),
                                          font.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                                          DWRITE_FONT_STRETCH_NORMAL, font.size, L"", &format));
            format->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
            auto wide = Wide(text);
            ComPtr<IDWriteTextLayout> layout;
            Check(fonts->CreateTextLayout(wide.data(), UINT32(wide.size()), format.Get(), std::max(1.f, width),
                                          std::max(1.f, height), &layout));
            layouts.emplace(std::move(key), layout);
            return layout;
        }
    };
    Canvas::Canvas() : impl_(std::make_unique<Impl>()) {}
    Canvas::~Canvas() = default;
    void Canvas::Begin(int width, int height, LWS::ContentScale scale)
    {
        auto& c = *impl_;
        width = std::max(1, width);
        height = std::max(1, height);
        c.lock.Reset();
        if (width != c.width || height != c.height)
        {
            c.target.Reset();
            c.bitmap.Reset();
            Check(
                c.imaging->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &c.bitmap));
            auto properties = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
            Check(c.factory->CreateWicBitmapRenderTarget(c.bitmap.Get(), properties, &c.target));
        }
        c.target->BeginDraw();
        c.target->SetTransform(D2D1::Matrix3x2F::Scale(float(scale.x), float(scale.y)));
        c.width = width;
        c.height = height;
        scale_ = scale;
    }
    void Canvas::Fill(float x, float y, float width, float height, LLUtils::Color color)
    {
        if (width <= 0 || height <= 0)
            return;
        // Per-draw brushes keep render-target ownership/recreation simple. Reuse
        // them only if profiling identifies brush creation as material frame cost.
        ComPtr<ID2D1SolidColorBrush> brush;
        Check(impl_->target->CreateSolidColorBrush(
            D2D1::ColorF(color.R() / 255.f, color.G() / 255.f, color.B() / 255.f, color.A() / 255.f), &brush));
        impl_->target->FillRectangle(D2D1::RectF(x, y, x + width, y + height), brush.Get());
    }
    void Canvas::Text(std::string_view text, float x, float y, float width, float height, LLUtils::Color color,
                      const FontSpec& font, bool wrap, std::span<const TextSpan> spans)
    {
        PaintText(text, x, y, width, height, color, font, wrap, spans, false);
    }
    void Canvas::CenteredText(std::string_view text, float x, float y, float width, float height, LLUtils::Color color,
                              const FontSpec& font, std::span<const TextSpan> spans)
    {
        PaintText(text, x, y, width, height, color, font, false, spans, true);
    }
    void Canvas::PaintText(std::string_view text, float x, float y, float width, float height, LLUtils::Color color,
                           const FontSpec& font, bool wrap, std::span<const TextSpan> spans, bool centered)
    {
        if (text.empty() || width <= 0 || height <= 0)
            return;
        ClipScope clip(*this, x, y, width, height);
        ComPtr<ID2D1SolidColorBrush> brush;
        Check(impl_->target->CreateSolidColorBrush(
            D2D1::ColorF(color.R() / 255.f, color.G() / 255.f, color.B() / 255.f, color.A() / 255.f), &brush));
        auto layout = impl_->Layout(text, width, height, font, wrap);
        if (centered)
        {
            DWRITE_OVERHANG_METRICS ink{};
            Check(layout->GetOverhangMetrics(&ink));
            const float inkWidth = layout->GetMaxWidth() + ink.left + ink.right;
            const float inkHeight = layout->GetMaxHeight() + ink.top + ink.bottom;
            x += std::max(0.f, (width - inkWidth) / 2) + ink.left;
            y += (height - inkHeight) / 2 + ink.top;
        }
        const auto wide = Wide(text);
        Check(layout->SetDrawingEffect(nullptr, {0, UINT32(wide.size())}));
        // Apply spans in order: selection ranges supplied last override search colors.
        // Range geometry comes from the complete shaped layout, including wrapped/bidi runs.
        for (const auto& span : spans)
        {
            auto first = Wide(text.substr(0, span.offset)).size();
            auto length = Wide(text.substr(span.offset, span.length)).size();
            UINT32 count = 0;
            layout->HitTestTextRange(UINT32(first), UINT32(length), x, y, nullptr, 0, &count);
            std::vector<DWRITE_HIT_TEST_METRICS> rectangles(count);
            if (count)
                Check(layout->HitTestTextRange(UINT32(first), UINT32(length), x, y, rectangles.data(), count, &count));
            for (const auto& r : rectangles)
                Fill(r.left, r.top, r.width, r.height, span.background);
            ComPtr<ID2D1SolidColorBrush> highlight;
            const auto c = span.foreground;
            Check(impl_->target->CreateSolidColorBrush(
                D2D1::ColorF(c.R() / 255.f, c.G() / 255.f, c.B() / 255.f, c.A() / 255.f), &highlight));
            Check(layout->SetDrawingEffect(highlight.Get(), {UINT32(first), UINT32(length)}));
        }
        impl_->target->DrawTextLayout(D2D1::Point2F(x, y), layout.Get(), brush.Get());
        Check(layout->SetDrawingEffect(nullptr, {0, UINT32(wide.size())}));
    }
    void Canvas::Line(float x1, float y1, float x2, float y2, float thickness, LLUtils::Color color)
    {
        if (thickness <= 0)
            return;
        ComPtr<ID2D1SolidColorBrush> brush;
        Check(impl_->target->CreateSolidColorBrush(
            D2D1::ColorF(color.R() / 255.f, color.G() / 255.f, color.B() / 255.f, color.A() / 255.f), &brush));
        impl_->target->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush.Get(), thickness);
    }
    void Canvas::FillCircle(float centerX, float centerY, float radius, LLUtils::Color color)
    {
        if (radius <= 0)
            return;
        ComPtr<ID2D1SolidColorBrush> brush;
        Check(impl_->target->CreateSolidColorBrush(
            D2D1::ColorF(color.R() / 255.f, color.G() / 255.f, color.B() / 255.f, color.A() / 255.f), &brush));
        impl_->target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(centerX, centerY), radius, radius), brush.Get());
    }
    void Canvas::StrokeCircle(float centerX, float centerY, float radius, float thickness, LLUtils::Color color)
    {
        if (radius <= 0 || thickness <= 0)
            return;
        ComPtr<ID2D1SolidColorBrush> brush;
        Check(impl_->target->CreateSolidColorBrush(
            D2D1::ColorF(color.R() / 255.f, color.G() / 255.f, color.B() / 255.f, color.A() / 255.f), &brush));
        impl_->target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(centerX, centerY), radius, radius), brush.Get(),
                                   thickness);
    }
    void Canvas::Image(const LWS::BitmapBuffer& source, float x, float y, float width, float height)
    {
        if (width <= 0 || height <= 0)
            return;
        LWS::Bitmap normalized(source);
        const auto image = normalized.GetBuffer();
        ComPtr<ID2D1Bitmap> bitmap;
        Check(impl_->target->CreateBitmap(D2D1::SizeU(image.width, image.height), image.pixels.data(), image.rowPitch,
                                          D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                                   D2D1_ALPHA_MODE_PREMULTIPLIED)),
                                          &bitmap));
        impl_->target->DrawBitmap(bitmap.Get(), D2D1::RectF(x, y, x + width, y + height));
    }
    float Canvas::Measure(std::string_view text, const FontSpec& font)
    {
        auto layout = impl_->Layout(text, 100000, 100000, font, false);
        DWRITE_TEXT_METRICS metrics{};
        Check(layout->GetMetrics(&metrics));
        return metrics.widthIncludingTrailingWhitespace;
    }
    size_t Canvas::HitTestText(std::string_view text, float x, const FontSpec& font)
    {
        auto layout = impl_->Layout(text, 100000, 100000, font, false);
        BOOL trailing = FALSE, inside = FALSE;
        DWRITE_HIT_TEST_METRICS metrics{};
        Check(layout->HitTestPoint(x, 0, &trailing, &inside, &metrics));
        const auto wide = Wide(text);
        const auto offset = std::min(size_t(metrics.textPosition) + (trailing ? metrics.length : 0), wide.size());
        return size_t(
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), int(offset), nullptr, 0, nullptr, nullptr));
    }
    float Canvas::Caret(std::string_view text, size_t byteOffset, const FontSpec& font)
    {
        auto layout = impl_->Layout(text, 100000, 100000, font, false);
        auto prefix = Wide(text.substr(0, byteOffset));
        float x = 0, y = 0;
        DWRITE_HIT_TEST_METRICS metrics{};
        Check(layout->HitTestTextPosition(UINT32(prefix.size()), false, &x, &y, &metrics));
        return x;
    }
    std::vector<TextLine> Canvas::TextLines(std::string_view text, float width, const FontSpec& font)
    {
        auto layout = impl_->Layout(text, width, 100000, font, true);
        UINT32 count = 0;
        layout->GetLineMetrics(nullptr, 0, &count);
        std::vector<DWRITE_LINE_METRICS> metrics(count);
        Check(layout->GetLineMetrics(metrics.data(), count, &count));
        const auto wide = Wide(text);
        const auto byteOffset = [&](size_t offset)
        { return size_t(WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(offset), nullptr, 0, nullptr, nullptr)); };
        std::vector<TextLine> lines;
        size_t offset = 0;
        float y = 0;
        for (const auto& line : metrics)
        {
            const auto first = byteOffset(offset);
            lines.push_back({{first, byteOffset(offset + line.length - line.newlineLength) - first}, y, line.height});
            offset += line.length;
            y += line.height;
        }
        return lines;
    }
    TextPosition Canvas::HitTestText(std::string_view text, float x, float y, float width, const FontSpec& font)
    {
        auto layout = impl_->Layout(text, width, 100000, font, true);
        BOOL trailing = FALSE, inside = FALSE;
        DWRITE_HIT_TEST_METRICS metrics{};
        Check(layout->HitTestPoint(x, y, &trailing, &inside, &metrics));
        const auto wide = Wide(text);
        const auto offset = std::min(size_t(metrics.textPosition) + (trailing ? metrics.length : 0), wide.size());
        const auto byte = size_t(
            WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(offset), nullptr, 0, nullptr, nullptr));
        return {byte, bool(trailing) && byte > 0 && text[byte - 1] != '\n'};
    }
    TextCaret Canvas::CaretBounds(std::string_view text, TextPosition position, float width, const FontSpec& font)
    {
        auto layout = impl_->Layout(text, width, 100000, font, true);
        position.offset = std::min(position.offset, text.size());
        const bool trailing = position.upstream && position.offset && text[position.offset - 1] != '\n';
        const auto offset = trailing ? PreviousCharacter(text, position.offset) : position.offset;
        const auto prefix = Wide(text.substr(0, offset));
        TextCaret result;
        DWRITE_HIT_TEST_METRICS metrics{};
        Check(layout->HitTestTextPosition(UINT32(prefix.size()), trailing, &result.x, &result.y, &metrics));
        result.height = metrics.height;
        return result;
    }
    float Canvas::TextHeight(std::string_view text, float width, const FontSpec& font, bool wrap)
    {
        auto layout = impl_->Layout(text, width, 100000, font, wrap);
        DWRITE_TEXT_METRICS metrics{};
        Check(layout->GetMetrics(&metrics));
        return metrics.height;
    }
    void Canvas::Clip(float x, float y, float width, float height)
    {
        impl_->target->PushAxisAlignedClip(D2D1::RectF(x, y, x + std::max(0.f, width), y + std::max(0.f, height)),
                                           D2D1_ANTIALIAS_MODE_ALIASED);
    }
    void Canvas::Unclip()
    {
        impl_->target->PopAxisAlignedClip();
    }
    LWS::BitmapBuffer Canvas::End()
    {
        auto& c = *impl_;
        Check(c.target->EndDraw());
        WICRect rect{0, 0, c.width, c.height};
        Check(c.bitmap->Lock(&rect, WICBitmapLockRead, &c.lock));
        UINT size = 0, stride = 0;
        BYTE* data = nullptr;
        Check(c.lock->GetDataPointer(&size, &data));
        Check(c.lock->GetStride(&stride));
        return {{reinterpret_cast<const std::byte*>(data), size},
                LWS::BitmapPixelFormat::Bgra8Premultiplied,
                LWS::BitmapRowOrder::TopDown,
                uint32_t(c.width),
                uint32_t(c.height),
                stride};
    }
}  // namespace LWSUI
