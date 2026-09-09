#include <LWS/WindowIcon.hpp>

namespace LWS
{
    std::expected<WindowIcon, Result> WindowIcon::FromBitmap(const BitmapBuffer& bitmap)
    {
        try
        {
            return WindowIcon(std::make_shared<Bitmap>(bitmap));
        }
        catch (...)
        {
            return std::unexpected(Result::InvalidArgument);
        }
    }
}  // namespace LWS
