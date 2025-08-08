#include "DragAndDropTarget.hpp"

#include <LWS/StringDefs.hpp>

#include <vector>

namespace LWS::internal
{
    DragAndDropTarget::DragAndDropTarget(HWND hwnd, DragDropCallback callback)
        : fHwnd(hwnd)
        , fCallback(std::move(callback))
    {
        fAttached = SUCCEEDED(RegisterDragDrop(fHwnd, this));
    }

    DragAndDropTarget::~DragAndDropTarget()
    {
        detach();
    }

    void DragAndDropTarget::detach()
    {
        if (fAttached && fHwnd != nullptr)
        {
            RevokeDragDrop(fHwnd);
            fAttached = false;
        }
    }

    HRESULT DragAndDropTarget::QueryInterface(REFIID riid, void** ppv)
    {
        if (ppv == nullptr)
        {
            return E_POINTER;
        }

        if (riid == IID_IUnknown || riid == IID_IDropTarget)
        {
            *ppv = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }

        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG DragAndDropTarget::AddRef()
    {
        return static_cast<ULONG>(InterlockedIncrement(&mRefCount));
    }

    ULONG DragAndDropTarget::Release()
    {
        LONG ref_count = InterlockedDecrement(&mRefCount);
        return static_cast<ULONG>(ref_count);
    }

    HRESULT DragAndDropTarget::DragEnter(IDataObject*, DWORD, POINTL, DWORD* pdwEffect)
    {
        if (pdwEffect != nullptr)
        {
            *pdwEffect &= DROPEFFECT_COPY;
        }

        return S_OK;
    }

    HRESULT DragAndDropTarget::DragOver(DWORD, POINTL, DWORD* pdwEffect)
    {
        if (pdwEffect != nullptr)
        {
            *pdwEffect &= DROPEFFECT_COPY;
        }

        return S_OK;
    }

    HRESULT DragAndDropTarget::DragLeave()
    {
        return S_OK;
    }

    HRESULT DragAndDropTarget::Drop(IDataObject* pdto, DWORD, POINTL, DWORD* pdwEffect)
    {
        openFilesFromDataObject(pdto);
        if (pdwEffect != nullptr)
        {
            *pdwEffect &= DROPEFFECT_COPY;
        }

        return S_OK;
    }

    void DragAndDropTarget::openFilesFromDataObject(IDataObject* pdto)
    {
        if (pdto == nullptr || !fCallback)
        {
            return;
        }

        FORMATETC format_etc{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM storage{};
        if (FAILED(pdto->GetData(&format_etc, &storage)))
        {
            return;
        }

        HDROP hdrop = static_cast<HDROP>(storage.hGlobal);
        UINT file_count = DragQueryFile(hdrop, 0xFFFFFFFF, nullptr, 0);
        for (UINT file_index = 0; file_index < file_count; ++file_index)
        {
            UINT char_count = DragQueryFile(hdrop, file_index, nullptr, 0);
            if (char_count == 0)
            {
                continue;
            }

            LWS::string_type file_path(static_cast<size_t>(char_count), LWS::char_type{});
            if (DragQueryFile(hdrop, file_index, file_path.data(), char_count + 1U) > 0)
            {
                fCallback(std::filesystem::path(std::move(file_path)));
            }
        }

        ReleaseStgMedium(&storage);
    }
}
