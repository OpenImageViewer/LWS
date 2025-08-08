#pragma once
#include <LWS/interfaces/backends.hpp>
#include <memory>

namespace LWS
{
    class Cursor
    {
    public:
        Cursor();
        explicit Cursor(std::unique_ptr<ICursorBackend> impl);
        virtual ~Cursor() = default;

        Cursor(const Cursor&) = delete;
        Cursor& operator=(const Cursor&) = delete;
        Cursor(Cursor&&) noexcept = default;
        Cursor& operator=(Cursor&&) noexcept = default;

        void setVisible(bool visible);
        void setCursorShape(CursorShape shape);
        void setCustomCursor(const BitmapBuffer& bmp);
        BackendId backendId() const;

        std::shared_ptr<ICursorBackend> getBackendShared() const;

    protected:
        std::unique_ptr<ICursorBackend> impl_;
    };
}
