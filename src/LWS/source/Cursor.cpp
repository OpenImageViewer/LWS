#include <LWS/Cursor.hpp>

#include <LWS/Bitmap.hpp>

#include <array>
#include <variant>

namespace LWS
{
    struct Cursor::Resource
    {
        struct Custom
        {
            std::shared_ptr<const Bitmap> bitmap;
            Point hotspot;
        };

        std::variant<CursorShape, Custom> value;
    };

    Cursor Cursor::FromShape(CursorShape shape)
    {
        static const auto standard = []
        {
            std::array<std::shared_ptr<const Resource>, static_cast<size_t>(CursorShape::AppStarting) + 1> resources;
            for (size_t i = 0; i < resources.size(); ++i)
                resources[i] = std::make_shared<Resource>(Resource{static_cast<CursorShape>(i)});
            return resources;
        }();
        const auto index = static_cast<size_t>(shape);
        if (index < standard.size())
            return Cursor(standard[index]);
        // Preserve the existing behavior for unknown values; backends provide their own fallback.
        return Cursor(std::make_shared<Resource>(Resource{shape}));
    }

    std::expected<Cursor, Result> Cursor::FromBitmap(const BitmapBuffer& bitmap, Point hotspot)
    {
        if (hotspot.x < 0 || hotspot.y < 0 || static_cast<uint32_t>(hotspot.x) >= bitmap.width ||
            static_cast<uint32_t>(hotspot.y) >= bitmap.height)
        {
            return std::unexpected(Result::InvalidArgument);
        }

        try
        {
            auto normalized = std::make_shared<Bitmap>(bitmap);
            return Cursor(std::make_shared<Resource>(Resource{Resource::Custom{std::move(normalized), hotspot}}));
        }
        catch (...)
        {
            return std::unexpected(Result::InvalidArgument);
        }
    }

    bool Cursor::IsCustom() const
    {
        return std::holds_alternative<Resource::Custom>(resource_->value);
    }

    CursorShape Cursor::Shape() const
    {
        return std::get<CursorShape>(resource_->value);
    }

    BitmapBuffer Cursor::BitmapData() const
    {
        return std::get<Resource::Custom>(resource_->value).bitmap->GetBuffer();
    }

    Point Cursor::Hotspot() const
    {
        return std::get<Resource::Custom>(resource_->value).hotspot;
    }
}  // namespace LWS
