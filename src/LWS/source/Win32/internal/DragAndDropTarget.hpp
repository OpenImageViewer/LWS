#pragma once

#include <Windows.h>
#include <ShlObj.h>

#include <filesystem>
#include <functional>

namespace LWS::internal
{
    using DragDropCallback = std::function<void(const std::filesystem::path&)>;

    class DragAndDropTarget : public IDropTarget
    {
    public:
        DragAndDropTarget(HWND hwnd, DragDropCallback callback);
        ~DragAndDropTarget();

        void detach();

        STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
        STDMETHODIMP_(ULONG) AddRef() override;
        STDMETHODIMP_(ULONG) Release() override;

        STDMETHODIMP DragEnter(IDataObject* pdto, DWORD grfKeyState, POINTL ptl, DWORD* pdwEffect) override;
        STDMETHODIMP DragOver(DWORD grfKeyState, POINTL ptl, DWORD* pdwEffect) override;
        STDMETHODIMP DragLeave() override;
        STDMETHODIMP Drop(IDataObject* pdto, DWORD grfKeyState, POINTL ptl, DWORD* pdwEffect) override;

    private:
        void openFilesFromDataObject(IDataObject* pdto);

        LONG mRefCount = 1;
        HWND fHwnd = nullptr;
        DragDropCallback fCallback;
        bool fAttached = false;
    };
}
