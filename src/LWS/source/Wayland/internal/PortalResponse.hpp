#pragma once
#include <LWS/FileDialog.hpp>
#include <dbus/dbus.h>
#include "UriList.hpp"
#include <string_view>
#include <utility>

namespace LWS::internal::portal
{
    // Validate the wire signature before interpreting any basic value as a pointer.
    // Publish results only after every URI has been decoded successfully.
    inline FileDialogResult DecodeResponse(DBusMessage* message, ListFileDialogFileNames& files)
    {
        if (!message || !dbus_message_has_signature(message, "ua{sv}"))
            return FileDialogResult::UnknownError;
        DBusMessageIter response;
        dbus_message_iter_init(message, &response);
        uint32_t code = 2;
        dbus_message_iter_get_basic(&response, &code);
        if (code != 0)
            return code == 1 ? FileDialogResult::UserCanceled : FileDialogResult::UnknownError;
        dbus_message_iter_next(&response);
        DBusMessageIter entries;
        dbus_message_iter_recurse(&response, &entries);
        ListFileDialogFileNames result;
        while (dbus_message_iter_get_arg_type(&entries) == DBUS_TYPE_DICT_ENTRY)
        {
            DBusMessageIter entry;
            dbus_message_iter_recurse(&entries, &entry);
            const char* key = nullptr;
            dbus_message_iter_get_basic(&entry, &key);
            dbus_message_iter_next(&entry);
            if (std::string_view(key) == "uris")
            {
                DBusMessageIter variant, uris;
                dbus_message_iter_recurse(&entry, &variant);
                if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_ARRAY ||
                    dbus_message_iter_get_element_type(&variant) != DBUS_TYPE_STRING)
                    return FileDialogResult::UnknownError;
                dbus_message_iter_recurse(&variant, &uris);
                while (dbus_message_iter_get_arg_type(&uris) == DBUS_TYPE_STRING)
                {
                    const char* uri = nullptr;
                    dbus_message_iter_get_basic(&uris, &uri);
                    auto path = parseFileUri(uri);
                    if (!path)
                        return FileDialogResult::UnknownError;
                    result.push_back(std::move(*path));
                    dbus_message_iter_next(&uris);
                }
            }
            dbus_message_iter_next(&entries);
        }
        if (result.empty())
            return FileDialogResult::UnknownError;
        files = std::move(result);
        return FileDialogResult::Success;
    }
}  // namespace LWS::internal::portal
