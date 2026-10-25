#ifdef LWS_PLATFORM_WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
    #include <ShlObj.h>

    #include <LWS/FileDialog.hpp>
    #include <LWS/Win32/WindowExtensions.hpp>
    #include "../internal/WindowBackendAccess.hpp"
    #include <LLUtils/Warnings.h>

    #include <sstream>
    #include <wrl/client.h>
    #include <filesystem>
    #include <memory>
    #include <vector>

namespace
{
    struct ComDlgFilterStorage
    {
        std::wstring name;
        std::wstring spec;
        COMDLG_FILTERSPEC filter{};
    };

    std::vector<ComDlgFilterStorage> BuildFilters(const LWS::ListFileDialogFilters& filters)
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
}  // namespace

namespace LWS
{
    namespace
    {
        FileDialogResult ShowNative(FileDialogType dialogType, const ListFileDialogFilters& filters,
                                    const file_dialog_string_type& title, Window& ownerWindow,
                                    const file_dialog_string_type& defaultExtension, uint32_t filterIndex,
                                    file_dialog_string_type defaultFileName, ListFileDialogFileNames& outFilenames,
                                    bool multiple)
        {
            if (!ownerWindow.IsCreated())
                return FileDialogResult::UnknownError;
            const internal::WindowBackendAccess::DispatchScope ownerDispatch(ownerWindow);
            const internal::PlatformContextAccess::DispatchScope contextDispatch(ownerWindow.GetPlatformContext());
            const auto owner = Win32::GetHwnd(ownerWindow);
            if (!owner.has_value())
                return FileDialogResult::UnknownError;
            constexpr auto failure = FileDialogResult::UnknownError;
            if (!filters.empty() && filterIndex > filters.size())
                return failure;
            const CLSID& dialogClassID = dialogType == FileDialogType::SaveFile ? CLSID_FileSaveDialog
                                                                                : CLSID_FileOpenDialog;
            HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            if (FAILED(hr))
                return failure;
            struct Apartment
            {
                ~Apartment() { CoUninitialize(); }
            } apartment;
            Microsoft::WRL::ComPtr<IFileDialog> pfd;
            LLUTILS_DISABLE_WARNING_PUSH
            LLUTILS_DISABLE_WARNING_LANGUAGE_EXTENSION
            hr = CoCreateInstance(dialogClassID, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
            LLUTILS_DISABLE_WARNING_POP
            if (FAILED(hr))
                return failure;
            DWORD flags{};
            if (FAILED(pfd->GetOptions(&flags)))
                return failure;
            flags |= FOS_FORCEFILESYSTEM;
            if (dialogType == FileDialogType::OpenFile)
                flags |= FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST;
            if (multiple && dialogType == FileDialogType::OpenFile)
                flags |= FOS_ALLOWMULTISELECT;
            if (FAILED(pfd->SetOptions(flags)) || FAILED(pfd->SetTitle(title.c_str())))
                return failure;
            if (!defaultFileName.empty())
            {
                std::filesystem::path initial(defaultFileName);
                std::error_code error;
                const bool directory = std::filesystem::is_directory(initial, error);
                // A bare filename preserves the shell's remembered folder.
                if (initial.has_parent_path() || directory)
                {
                    const auto absolute = std::filesystem::absolute(initial, error);
                    if (!error)
                        initial = absolute;
                }
                const auto folder = directory ? initial : initial.parent_path();
                if (!folder.empty())
                {
                    Microsoft::WRL::ComPtr<IShellItem> item;
                    if (SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&item))) &&
                        FAILED(pfd->SetFolder(item.Get())))
                        return failure;
                }
                if (!directory && FAILED(pfd->SetFileName(initial.filename().c_str())))
                    return failure;
            }
            auto filterStorage = BuildFilters(filters);
            std::vector<COMDLG_FILTERSPEC> comFilters;
            comFilters.reserve(filterStorage.size());
            for (const auto& filter : filterStorage)
                comFilters.push_back(filter.filter);
            if (!comFilters.empty() &&
                (FAILED(pfd->SetFileTypes(static_cast<UINT>(comFilters.size()), comFilters.data())) ||
                 FAILED(pfd->SetFileTypeIndex(filterIndex == 0 ? 1 : filterIndex))))
                return failure;
            if (!defaultExtension.empty() && FAILED(pfd->SetDefaultExtension(defaultExtension.c_str())))
                return failure;
            hr = pfd->Show(*owner);
            if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
                return FileDialogResult::UserCanceled;
            if (FAILED(hr))
                return failure;
            ListFileDialogFileNames selected;
            auto appendPath = [&](IShellItem* item)
            {
                PWSTR filePath = nullptr;
                const auto status = item->GetDisplayName(SIGDN_FILESYSPATH, &filePath);
                std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> ownedPath(filePath, &CoTaskMemFree);
                if (SUCCEEDED(status))
                    selected.emplace_back(filePath);
                return status;
            };
            if (multiple && dialogType == FileDialogType::OpenFile)
            {
                Microsoft::WRL::ComPtr<IFileOpenDialog> openDialog;
                Microsoft::WRL::ComPtr<IShellItemArray> items;
                if (FAILED(pfd.As(&openDialog)) || FAILED(openDialog->GetResults(&items)))
                    return failure;
                DWORD count = 0;
                if (FAILED(items->GetCount(&count)))
                    return failure;
                for (DWORD i = 0; i < count; ++i)
                {
                    Microsoft::WRL::ComPtr<IShellItem> item;
                    if (FAILED(items->GetItemAt(i, &item)) || FAILED(appendPath(item.Get())))
                        return failure;
                }
            }
            else
            {
                Microsoft::WRL::ComPtr<IShellItem> item;
                if (FAILED(pfd->GetResult(&item)) || FAILED(appendPath(item.Get())))
                    return failure;
            }
            if (selected.empty())
                return failure;
            outFilenames = std::move(selected);
            return FileDialogResult::Success;
        }
    }  // namespace
    FileDialogResult FileDialog::Show(FileDialogType dialogType, const ListFileDialogFilters& filters,
                                      const file_dialog_string_type& title, Window& ownerWindow,
                                      const file_dialog_string_type& defaultExtension, uint32_t filterIndex,
                                      file_dialog_string_type defaultFileName, file_dialog_string_type& outFilename)
    {
        ListFileDialogFileNames files;
        const auto result = ShowNative(dialogType, filters, title, ownerWindow, defaultExtension, filterIndex,
                                       std::move(defaultFileName), files, false);
        if (result == FileDialogResult::Success)
            outFilename = std::move(files.front());
        return result;
    }
    FileDialogResult FileDialog::Show(FileDialogType dialogType, const ListFileDialogFilters& filters,
                                      const file_dialog_string_type& title, Window& ownerWindow,
                                      const file_dialog_string_type& defaultExtension, uint32_t filterIndex,
                                      file_dialog_string_type defaultFileName, ListFileDialogFileNames& outFilenames)
    {
        return ShowNative(dialogType, filters, title, ownerWindow, defaultExtension, filterIndex,
                          std::move(defaultFileName), outFilenames, true);
    }
}  // namespace LWS
#endif
