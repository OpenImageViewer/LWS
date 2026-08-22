#pragma once

#include <LWS/interfaces/backends.hpp>

#include <optional>
#include <vector>

namespace LWS
{
    enum class FileDialogType
    {
        Unspecified,
        OpenFile,
        SaveFile,
    };

    using file_dialog_string_type = string_type;
    using ListFileDialogFileNames = std::vector<file_dialog_string_type>;

    enum class FileDialogResult
    {
        Success,
        UserCanceled,
        UnknownError,
    };

    class FileDialogFilterBuilder
    {
    public:
        using ListExtensions = std::vector<file_dialog_string_type>;

        struct FileDialogFilter
        {
            file_dialog_string_type description;
            ListExtensions extensions;
        };

        using ListFileDialogFilters = std::vector<FileDialogFilter>;

        FileDialogFilterBuilder() = default;
        explicit FileDialogFilterBuilder(const ListFileDialogFilters& filters);

        [[nodiscard]] const ListFileDialogFilters& GetFilters() const;

    private:
        ListFileDialogFilters fFilters;
    };

    class FileDialog
    {
    public:
        static FileDialogResult Show(FileDialogType dialogType,
                                     const FileDialogFilterBuilder::ListFileDialogFilters& filters,
                                     const file_dialog_string_type& title,
                                     Handle ownerWindow,
                                     const file_dialog_string_type& defaultExtension,
                                     uint32_t filterIndex,
                                     file_dialog_string_type defaultFileName,
                                     file_dialog_string_type& outFilename);

        static FileDialogResult Show(FileDialogType dialogType,
                                     const FileDialogFilterBuilder::ListFileDialogFilters& filters,
                                     const file_dialog_string_type& title,
                                     Handle ownerWindow,
                                     const file_dialog_string_type& defaultExtension,
                                     uint32_t filterIndex,
                                     file_dialog_string_type defaultFileName,
                                     ListFileDialogFileNames& outFilenames);
    };
}
