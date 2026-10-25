#pragma once

#include <LWS/Window.hpp>

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

    struct FileDialogFilter
    {
        file_dialog_string_type description;
        std::vector<file_dialog_string_type> extensions;
    };

    using ListFileDialogFilters = std::vector<FileDialogFilter>;

    class FileDialog
    {
      public:

        // Select one path. Outputs are replaced only on success.
        static FileDialogResult Show(FileDialogType dialogType, const ListFileDialogFilters& filters,
                                     const file_dialog_string_type& title, Window& ownerWindow,
                                     const file_dialog_string_type& defaultExtension, uint32_t filterIndex,
                                     file_dialog_string_type defaultFileName, file_dialog_string_type& outFilename);

        // OpenFile permits multiple selections; SaveFile still selects one path.
        static FileDialogResult Show(FileDialogType dialogType, const ListFileDialogFilters& filters,
                                     const file_dialog_string_type& title, Window& ownerWindow,
                                     const file_dialog_string_type& defaultExtension, uint32_t filterIndex,
                                     file_dialog_string_type defaultFileName, ListFileDialogFileNames& outFilenames);
    };
}  // namespace LWS
