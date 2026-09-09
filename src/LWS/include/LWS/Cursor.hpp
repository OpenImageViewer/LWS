#pragma once

#include <LWS/Bitmap.hpp>
#include <LWS/CursorShape.hpp>
#include <LWS/Result.hpp>
#include <LWS/WindowTypes.hpp>

#include <expected>
#include <memory>

namespace LWS
{
    /// Immutable logical cursor data with no context or native-handle affinity.
    ///
    /// @par Thread safety
    /// Factories, copies, moves, and destruction are thread-neutral. Applying a cursor remains window-thread-affine.
    class Cursor final
    {
      public:

        Cursor(const Cursor&) noexcept = default;
        Cursor& operator=(const Cursor&) noexcept = default;
        Cursor(Cursor&&) noexcept = default;
        Cursor& operator=(Cursor&&) noexcept = default;

        [[nodiscard]] static Cursor FromShape(CursorShape shape);
        [[nodiscard]] static std::expected<Cursor, Result> FromBitmap(const BitmapBuffer& bitmap, Point hotspot);

      private:

        friend class Window;

        struct Resource;
        explicit Cursor(std::shared_ptr<const Resource> resource) : resource_(std::move(resource)) {}

        [[nodiscard]] bool IsCustom() const;
        [[nodiscard]] CursorShape Shape() const;
        [[nodiscard]] BitmapBuffer BitmapData() const;
        [[nodiscard]] Point Hotspot() const;

        std::shared_ptr<const Resource> resource_;
    };
}  // namespace LWS
