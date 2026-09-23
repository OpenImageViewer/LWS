#ifdef LWS_PLATFORM_WAYLAND
    #include <LWS/FileDialog.hpp>
    #include <LWS/Platform.hpp>
    #include "internal/WindowBackendWayland.hpp"
    #include "../internal/WindowBackendAccess.hpp"
    #include "internal/PlatformState.hpp"
    #include "internal/PortalResponse.hpp"
    #include <dbus/dbus.h>
    #include <atomic>
    #include <memory>

namespace LWS
{
    namespace
    {
        struct ConnectionDeleter
        {
            void operator()(DBusConnection* c) const
            {
                dbus_connection_close(c);
                dbus_connection_unref(c);
            }
        };
        struct MessageDeleter
        {
            void operator()(DBusMessage* m) const
            {
                if (m)
                    dbus_message_unref(m);
            }
        };
        using Message = std::unique_ptr<DBusMessage, MessageDeleter>;
        bool IsPortalText(const std::string& text)
        {
            return text.find('\0') == std::string::npos && dbus_validate_utf8(text.c_str(), nullptr);
        }
        void String(DBusMessageIter& iter, const std::string& text)
        {
            const char* value = text.c_str();
            dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &value);
        }
        void Option(DBusMessageIter& dict, const char* key, const std::string& value)
        {
            DBusMessageIter entry, variant;
            dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
            dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
            dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
            String(variant, value);
            dbus_message_iter_close_container(&entry, &variant);
            dbus_message_iter_close_container(&dict, &entry);
        }
        void PathOption(DBusMessageIter& dict, const char* key, const std::filesystem::path& path)
        {
            const auto& bytes = path.native();
            DBusMessageIter entry, variant, array;
            dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
            dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
            dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "ay", &variant);
            dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "y", &array);
            const char* data = bytes.c_str();
            dbus_message_iter_append_fixed_array(&array, DBUS_TYPE_BYTE, &data, int(bytes.size() + 1));
            dbus_message_iter_close_container(&variant, &array);
            dbus_message_iter_close_container(&entry, &variant);
            dbus_message_iter_close_container(&dict, &entry);
        }
        void Filter(DBusMessageIter& parent, const FileDialogFilter& filter)
        {
            DBusMessageIter item, patterns;
            dbus_message_iter_open_container(&parent, DBUS_TYPE_STRUCT, nullptr, &item);
            String(item, filter.description);
            dbus_message_iter_open_container(&item, DBUS_TYPE_ARRAY, "(us)", &patterns);
            for (auto pattern : filter.extensions)
            {
                if (pattern.find('*') == std::string::npos)
                    pattern = pattern.starts_with(".") ? "*" + pattern : "*." + pattern;
                DBusMessageIter p;
                uint32_t glob = 0;
                dbus_message_iter_open_container(&patterns, DBUS_TYPE_STRUCT, nullptr, &p);
                dbus_message_iter_append_basic(&p, DBUS_TYPE_UINT32, &glob);
                String(p, pattern);
                dbus_message_iter_close_container(&patterns, &p);
            }
            dbus_message_iter_close_container(&item, &patterns);
            dbus_message_iter_close_container(&parent, &item);
        }
        std::string WithDefaultExtension(std::string name, std::string_view extension)
        {
            if (!name.empty() && !extension.empty() && !std::filesystem::path(name).has_extension())
            {
                if (extension.front() != '.')
                    name += '.';
                name += extension;
            }
            return name;
        }
        // Without xdg-foreign export the native portal is unparented. Keep the
        // required owner modal locally; upstream dispatch scopes retain its backend.
        struct ModalOwner
        {
            WindowBackendWayland& window;
            explicit ModalOwner(WindowBackendWayland& owner) : window(owner) { window.beginModalInput(); }
            ~ModalOwner() { window.endModalInput(); }
        };
        // The modal pump can run callbacks that mutate the caller's option storage.
        // Own one stable snapshot for this request and any overwrite-confirmation retry.
        FileDialogResult ShowPortal(FileDialogType type, ListFileDialogFilters filters, std::string title,
                                    Window& ownerWindow, std::string defaultExtension, uint32_t filterIndex,
                                    std::string initial, ListFileDialogFileNames& files, bool multiple)
        {
            if (type != FileDialogType::OpenFile && type != FileDialogType::SaveFile)
                return FileDialogResult::UnknownError;
            if (!filters.empty() && filterIndex > filters.size())
                return FileDialogResult::UnknownError;
            // D-Bus string marshalling asserts on invalid UTF-8. Validate external
            // text before starting a request; native folder/file paths use byte arrays.
            if (!IsPortalText(title) || !IsPortalText(defaultExtension) || initial.find('\0') != std::string::npos)
                return FileDialogResult::UnknownError;
            for (const auto& filter : filters)
            {
                if (!IsPortalText(filter.description))
                    return FileDialogResult::UnknownError;
                for (const auto& pattern : filter.extensions)
                    if (!IsPortalText(pattern))
                        return FileDialogResult::UnknownError;
            }
            if (!ownerWindow.IsCreated())
                return FileDialogResult::UnknownError;
            const internal::WindowBackendAccess::DispatchScope ownerDispatch(ownerWindow);
            auto& context = ownerWindow.GetPlatformContext();
            const internal::PlatformContextAccess::DispatchScope contextDispatch(context);
            auto* owner = dynamic_cast<WindowBackendWayland*>(internal::WindowBackendAccess::Get(ownerWindow));
            if (!owner)
                return FileDialogResult::UnknownError;
            ModalOwner modal(*owner);
            for (;;)
            {
                std::filesystem::path path(initial);
                std::error_code pathError;
                const bool directory = !initial.empty() && std::filesystem::is_directory(path, pathError);
                std::string currentName;
                if (!initial.empty() && type == FileDialogType::SaveFile && !directory)
                {
                    path = WithDefaultExtension(path.string(), defaultExtension);
                    currentName = path.filename().string();
                    if (!IsPortalText(currentName))
                        return FileDialogResult::UnknownError;
                }
                DBusError error;
                dbus_error_init(&error);
                std::unique_ptr<DBusConnection, ConnectionDeleter> connection(
                    dbus_bus_get_private(DBUS_BUS_SESSION, &error));
                dbus_error_free(&error);
                if (!connection)
                    return FileDialogResult::UnknownError;
                dbus_connection_set_exit_on_disconnect(connection.get(), false);
                // Subscribe before issuing the request so a fast response cannot be lost.
                dbus_bus_add_match(connection.get(),
                                   "type='signal',sender='org.freedesktop.portal.Desktop',interface='org.freedesktop."
                                   "portal.Request',member='Response'",
                                   &error);
                if (!dbus_error_is_set(&error))
                    dbus_bus_add_match(connection.get(),
                                       "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
                                       "member='NameOwnerChanged',arg0='org.freedesktop.portal.Desktop'",
                                       &error);
                if (dbus_error_is_set(&error))
                {
                    dbus_error_free(&error);
                    return FileDialogResult::UnknownError;
                }
                Message call(dbus_message_new_method_call("org.freedesktop.portal.Desktop",
                                                          "/org/freedesktop/portal/desktop",
                                                          "org.freedesktop.portal.FileChooser",
                                                          type == FileDialogType::SaveFile ? "SaveFile" : "OpenFile"));
                DBusMessageIter args, dict;
                dbus_message_iter_init_append(call.get(), &args);
                String(args, "");  // See ModalOwner: no exported xdg-foreign parent is available.
                String(args, title);
                dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &dict);
                static std::atomic<uint64_t> sequence{0};
                Option(dict, "handle_token", "lws" + std::to_string(++sequence));
                if (!initial.empty())
                {
                    std::error_code ec;
                    // Preserve the portal's remembered folder for a bare suggested name.
                    if (directory || path.has_parent_path() || type == FileDialogType::OpenFile)
                    {
                        auto absolute = std::filesystem::absolute(path, ec);
                        if (!ec)
                        {
                            // OpenFile supports a starting folder, but no preselected filename.
                            PathOption(dict, "current_folder", directory ? absolute : absolute.parent_path());
                            if (type == FileDialogType::SaveFile && !directory &&
                                std::filesystem::is_regular_file(absolute, ec))
                                PathOption(dict, "current_file", absolute);
                        }
                    }
                    if (type == FileDialogType::SaveFile && !directory)
                        Option(dict, "current_name", currentName);
                }
                if (type == FileDialogType::OpenFile && multiple)
                {
                    const char* key = "multiple";
                    dbus_bool_t yes = true;
                    DBusMessageIter entry, variant;
                    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
                    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
                    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &variant);
                    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &yes);
                    dbus_message_iter_close_container(&entry, &variant);
                    dbus_message_iter_close_container(&dict, &entry);
                }
                if (!filters.empty())
                {
                    const char* key = "filters";
                    DBusMessageIter entry, variant, array;
                    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
                    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
                    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "a(sa(us))", &variant);
                    dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "(sa(us))", &array);
                    for (const auto& filter : filters)
                        Filter(array, filter);
                    dbus_message_iter_close_container(&variant, &array);
                    dbus_message_iter_close_container(&entry, &variant);
                    dbus_message_iter_close_container(&dict, &entry);
                    key = "current_filter";
                    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
                    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
                    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "(sa(us))", &variant);
                    Filter(variant, filters[filterIndex == 0 ? 0 : filterIndex - 1]);
                    dbus_message_iter_close_container(&entry, &variant);
                    dbus_message_iter_close_container(&dict, &entry);
                }
                dbus_message_iter_close_container(&args, &dict);
                Message reply(dbus_connection_send_with_reply_and_block(connection.get(), call.get(), 5000, &error));
                if (!reply)
                {
                    dbus_error_free(&error);
                    return FileDialogResult::UnknownError;
                }
                const char* request = nullptr;
                if (!dbus_message_get_args(reply.get(), &error, DBUS_TYPE_OBJECT_PATH, &request, DBUS_TYPE_INVALID))
                {
                    dbus_error_free(&error);
                    return FileDialogResult::UnknownError;
                }
                const char* sender = dbus_message_get_sender(reply.get());
                if (!sender || !*sender)
                    return FileDialogResult::UnknownError;
                const std::string requestSender = sender;
                std::string requestPath = request;
                // Deferred: integrate D-Bus watches/timeouts when dialogs gain an asynchronous
                // request lifetime. The synchronous API currently needs this nested pump;
                // a partial fd-only conversion would omit D-Bus timeouts and dispatch rules.
                for (;;)
                {
                    const auto loop = context.ProcessMessages();
                    if (loop != LoopResult::Continue || !ownerWindow.IsCreated())
                    {
                        Message close(dbus_message_new_method_call(requestSender.c_str(), requestPath.c_str(),
                                                                   "org.freedesktop.portal.Request", "Close"));
                        dbus_connection_send(connection.get(), close.get(), nullptr);
                        dbus_connection_flush(connection.get());
                        return loop == LoopResult::Failed ? FileDialogResult::UnknownError
                                                          : FileDialogResult::UserCanceled;
                    }
                    if (!dbus_connection_read_write(connection.get(), 10))
                        return FileDialogResult::UnknownError;
                    Message signal(dbus_connection_pop_message(connection.get()));
                    if (signal && dbus_message_has_sender(signal.get(), "org.freedesktop.DBus") &&
                        dbus_message_is_signal(signal.get(), "org.freedesktop.DBus", "NameOwnerChanged"))
                    {
                        const char *name = nullptr, *oldOwner = nullptr, *newOwner = nullptr;
                        if (dbus_message_get_args(signal.get(), nullptr, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING,
                                                  &oldOwner, DBUS_TYPE_STRING, &newOwner, DBUS_TYPE_INVALID) &&
                            std::string_view(name) == "org.freedesktop.portal.Desktop" && oldOwner == requestSender &&
                            newOwner != requestSender)
                            return FileDialogResult::UnknownError;
                    }
                    // Bus match rules do not exclude directly addressed signals.
                    if (!signal || !dbus_message_has_sender(signal.get(), requestSender.c_str()) ||
                        !dbus_message_is_signal(signal.get(), "org.freedesktop.portal.Request", "Response") ||
                        !dbus_message_has_path(signal.get(), requestPath.c_str()))
                        continue;
                    ListFileDialogFileNames result;
                    const auto status = internal::portal::DecodeResponse(signal.get(), result);
                    if (status != FileDialogResult::Success)
                        return status;
                    if (type == FileDialogType::SaveFile)
                    {
                        auto extended = WithDefaultExtension(result.front(), defaultExtension);
                        if (extended != result.front())
                        {
                            std::error_code ec;
                            const bool exists = std::filesystem::exists(extended, ec);
                            if (ec)
                                return FileDialogResult::UnknownError;
                            if (exists)
                            {
                                // A suffix added after acceptance must not bypass the
                                // portal's overwrite confirmation for the actual target.
                                initial = std::move(extended);
                                break;
                            }
                            result.front() = std::move(extended);
                        }
                    }
                    files = std::move(result);
                    return FileDialogResult::Success;
                }
            }
        }
    }  // namespace
    FileDialogResult FileDialog::Show(FileDialogType type, const ListFileDialogFilters& filters,
                                      const file_dialog_string_type& title, Window& ownerWindow,
                                      const file_dialog_string_type& defaultExtension, uint32_t filterIndex,
                                      file_dialog_string_type initial, file_dialog_string_type& filename)
    {
        ListFileDialogFileNames files;
        auto result = ShowPortal(type, filters, title, ownerWindow, defaultExtension, filterIndex, std::move(initial),
                                 files, false);
        if (result == FileDialogResult::Success)
            filename = std::move(files.front());
        return result;
    }
    FileDialogResult FileDialog::Show(FileDialogType type, const ListFileDialogFilters& filters,
                                      const file_dialog_string_type& title, Window& ownerWindow,
                                      const file_dialog_string_type& defaultExtension, uint32_t filterIndex,
                                      file_dialog_string_type initial, ListFileDialogFileNames& filenames)
    {
        return ShowPortal(type, filters, title, ownerWindow, defaultExtension, filterIndex, std::move(initial),
                          filenames, true);
    }
}  // namespace LWS
#endif
