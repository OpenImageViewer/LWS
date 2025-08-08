#include <LWS/Cursor.hpp>
#include <stdexcept>

namespace LWS
{
    Cursor::Cursor()
        : impl_(internal::createDefaultCursorBackend())
    {
    }

    Cursor::Cursor(std::unique_ptr<ICursorBackend> impl)
        : impl_(std::move(impl))
    {
    }

    void Cursor::setVisible(bool visible) { impl_->setVisible(visible); }
    void Cursor::setCursorShape(CursorShape shape) { impl_->setCursorShape(shape); }
    void Cursor::setCustomCursor(const BitmapBuffer& bitmap) { impl_->setCustomCursor(bitmap); }
    BackendId Cursor::backendId() const { return impl_->backend(); }

    std::shared_ptr<ICursorBackend> Cursor::getBackendShared() const
    {
        return std::shared_ptr<ICursorBackend>(impl_.get(), [](ICursorBackend*) {});
    }
}
