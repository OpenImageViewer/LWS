#pragma once

#include <LWS/Bitmap.hpp>
#include <LWS/Result.hpp>

#include <expected>
#include <memory>

namespace LWS
{
    /// Immutable copied icon pixels with no context or native-handle affinity.
    ///
    /// @par Thread safety
    /// Factories, copies, moves, and destruction are thread-neutral. Applying an icon remains window-thread-affine.
    class WindowIcon final
    {
      public:

        [[nodiscard]] static std::expected<WindowIcon, Result> FromBitmap(const BitmapBuffer& bitmap);

        WindowIcon(const WindowIcon&) noexcept = default;
        WindowIcon& operator=(const WindowIcon&) noexcept = default;
        WindowIcon(WindowIcon&&) noexcept = default;
        WindowIcon& operator=(WindowIcon&&) noexcept = default;

      private:

        friend class Window;

        explicit WindowIcon(std::shared_ptr<const Bitmap> bitmap) : bitmap_(std::move(bitmap)) {}
        [[nodiscard]] BitmapBuffer GetBuffer() const { return bitmap_->GetBuffer(); }

        std::shared_ptr<const Bitmap> bitmap_;
    };
}  // namespace LWS
