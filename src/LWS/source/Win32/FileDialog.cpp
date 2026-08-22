#ifdef LWS_PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ShlObj.h>

#include <LWS/FileDialog.hpp>
#include <LLUtils/Warnings.h>

#include <sstream>
#include <vector>

namespace
{
    struct ComDlgFilterStorage
    {
        std::wstring name;
        std::wstring spec;
        COMDLG_FILTERSPEC filter{};
    };

    std::vector<ComDlgFilterStorage> BuildFilters(const LWS::FileDialogFilterBuilder::ListFileDialogFilters& filters)
    {
        std::vector<ComDlgFilterStorage> storage(filters.size());
        for (size_t i = 0; i < filters.size(); ++i)
        {
            storage[i].name = filters[i].description;
            std::wstringstream extBuffer;
            for (auto& extension : filters[i].extensions)
            {
                extBuffer << extension << L';';
            }

            if (extBuffer.rdbuf()->in_avail() > 0)
            {
                extBuffer.seekp(-1, std::ios_base::end);
                extBuffer << L'\0';
            }

            storage[i].spec = extBuffer.str();
            storage[i].filter.pszName = storage[i].name.c_str();
            storage[i].filter.pszSpec = storage[i].spec.c_str();
        }

        return storage;
    }
}

namespace LWS
{
    FileDialogFilterBuilder::FileDialogFilterBuilder(const ListFileDialogFilters& filters) : fFilters(filters) {}

    const FileDialogFilterBuilder::ListFileDialogFilters& FileDialogFilterBuilder::GetFilters() const
    {
        return fFilters;
    }

    FileDialogResult FileDialog::Show(FileDialogType dialogType,
                                      const FileDialogFilterBuilder::ListFileDialogFilters& filters,
                                      const file_dialog_string_type& title,
                                      Handle ownerWindow,
                                      const file_dialog_string_type& defaultExtension,
                                      uint32_t filterIndex,
                                      file_dialog_string_type defaultFileName,
                                      file_dialog_string_type& outFilename)
    {
        ListFileDialogFileNames fileNames;
        FileDialogResult result = Show(dialogType, filters, title, ownerWindow, defaultExtension, filterIndex,
                                       std::move(defaultFileName), fileNames);
        if (result == FileDialogResult::Success && !fileNames.empty())
        {
            outFilename = fileNames.back();
        }

        return result;
    }

    FileDialogResult FileDialog::Show(FileDialogType dialogType,
                                      const FileDialogFilterBuilder::ListFileDialogFilters& filters,
                                      const file_dialog_string_type& title,
                                      Handle ownerWindow,
                                      const file_dialog_string_type& defaultExtension,
                                      uint32_t filterIndex,
                                      file_dialog_string_type defaultFileName,
                                      ListFileDialogFileNames& outFilenames)
    {
        FileDialogResult result = FileDialogResult::UnknownError;
        IFileDialog* pfd = nullptr;
        const CLSID& dialogClassID = dialogType == FileDialogType::OpenFile ? CLSID_FileOpenDialog
                                      : dialogType == FileDialogType::SaveFile ? CLSID_FileSaveDialog
                                                                               : CLSID_FileOpenDialog;
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (SUCCEEDED(hr))
        {
LLUTILS_DISABLE_WARNING_PUSH
LLUTILS_DISABLE_WARNING_LANGUAGE_EXTENSION
            hr = CoCreateInstance(dialogClassID, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
LLUTILS_DISABLE_WARNING_POP
            if (SUCCEEDED(hr))
            {
                DWORD flags{};
                hr = pfd->GetOptions(&flags);
                if (SUCCEEDED(hr))
                {
                    hr = pfd->SetOptions(flags | FOS_FORCEFILESYSTEM);
                }

                if (SUCCEEDED(hr))
                {
                    pfd->SetTitle(title.c_str());
                    pfd->SetFileName(defaultFileName.c_str());

                    auto filterStorage = BuildFilters(filters);
                    std::vector<COMDLG_FILTERSPEC> comFilters;
                    comFilters.reserve(filterStorage.size());
                    for (const auto& filter : filterStorage)
                    {
                        comFilters.push_back(filter.filter);
                    }

                    if (!comFilters.empty())
                    {
                        hr = pfd->SetFileTypes(static_cast<UINT>(comFilters.size()), comFilters.data());
                    }

                    if (SUCCEEDED(hr))
                    {
                        hr = pfd->SetFileTypeIndex(filterIndex);
                    }
                    if (SUCCEEDED(hr))
                    {
                        hr = pfd->SetDefaultExtension(defaultExtension.c_str());
                    }
                    if (SUCCEEDED(hr))
                    {
                        hr = pfd->Show(reinterpret_cast<HWND>(ownerWindow));
                    }

                    if (SUCCEEDED(hr))
                    {
                        IShellItem* resultItem = nullptr;
                        hr = pfd->GetResult(&resultItem);
                        if (SUCCEEDED(hr))
                        {
                            PWSTR filePath = nullptr;
                            hr = resultItem->GetDisplayName(SIGDN_FILESYSPATH, &filePath);
                            if (SUCCEEDED(hr))
                            {
                                outFilenames.push_back(filePath);
                                CoTaskMemFree(filePath);
                                result = FileDialogResult::Success;
                            }
                            resultItem->Release();
                        }
                    }
                    else if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
                    {
                        result = FileDialogResult::UserCanceled;
                    }
                }

                pfd->Release();
            }
        }
        CoUninitialize();
        return result;
    }
}
#endif
