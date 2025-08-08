#pragma once
#ifdef LWS_PLATFORM_WIN32

#include <Windows.h>

#include <LWS/interfaces/backends.hpp>

namespace LWS
{
    class CursorBackendWin32 : public ICursorBackend
    {
    public:
        CursorBackendWin32() = default;
        ~CursorBackendWin32() override;

        void setVisible(bool visible) override;
        void setCursorShape(CursorShape shape) override;
        void setCustomCursor(const BitmapBuffer& bmp) override;
        BackendId backend() const override;

        HCURSOR getCursorHandle() const;

    private:
        HCURSOR fCurrentCursor = nullptr;
        HCURSOR fCustomCursor = nullptr;
        bool fVisible = true;
    };
}
#endif