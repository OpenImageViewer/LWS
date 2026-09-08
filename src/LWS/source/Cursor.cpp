#include <LWS/Cursor.hpp>
#include <stdexcept>

namespace LWS
{
    Cursor::Cursor() : impl_(internal::createDefaultCursorBackend()) {}

    Cursor::Cursor(std::unique_ptr<internal::ICursorBackend> impl) : impl_(std::move(impl)) {}

    void Cursor::setVisible(bool visible)
    {
        impl_->setVisible(visible);
    }
    void Cursor::setCursorShape(CursorShape shape)
    {
        impl_->setCursorShape(shape);
    }
    Result Cursor::setCustomCursor(const BitmapBuffer& bitmap)
    {
        return impl_->setCustomCursor(bitmap, {});
    }
    BackendId Cursor::backendId() const
    {
        return impl_->backend();
    }

    std::shared_ptr<internal::ICursorBackend> Cursor::getBackendShared() const
    {
        return impl_;
    }
}  // namespace LWS
