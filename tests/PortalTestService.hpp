#pragma once
#include <dbus/dbus.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// A real service on a private test bus. It inspects requests and sends protocol replies, without
// opening desktop UI or replacing production functions. Run with dbus-run-session, never on a user's bus.
class PortalTestService final
{
  public:

    enum class Response
    {
        Accept,
        Cancel,
        Error,
        OwnerClose
    };
    struct Request
    {
        std::string method, title, currentName, selectedFilter;
        std::vector<std::string> selectedPatterns;
        bool multiple{};
        unsigned filters{};
    };
    PortalTestService()
    {
        dbus_threads_init_default();
        DBusError error;
        dbus_error_init(&error);
        connection_ = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
        dbus_error_free(&error);
        if (!connection_)
            throw std::runtime_error("private D-Bus connection unavailable");
        dbus_connection_set_exit_on_disconnect(connection_, false);
        const int result = dbus_bus_request_name(connection_, "org.freedesktop.portal.Desktop",
                                                 DBUS_NAME_FLAG_DO_NOT_QUEUE, &error);
        dbus_error_free(&error);
        if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
        {
            dbus_connection_close(connection_);
            dbus_connection_unref(connection_);
            throw std::runtime_error("test portal name unavailable");
        }
        thread_ = std::jthread(
            [this](std::stop_token stop)
            {
                while (!stop.stop_requested() && dbus_connection_read_write(connection_, 10))
                {
                    while (auto* message = dbus_connection_pop_message(connection_))
                    {
                        Handle(message);
                        dbus_message_unref(message);
                    }
                }
            });
    }
    ~PortalTestService()
    {
        thread_.request_stop();
        thread_.join();
        dbus_connection_close(connection_);
        dbus_connection_unref(connection_);
    }
    void SetResponse(Response response) { response_ = response; }
    Request LastRequest()
    {
        const std::scoped_lock lock(mutex_);
        return last_;
    }

  private:

    static std::string String(DBusMessageIter iterator)
    {
        const char* value = nullptr;
        dbus_message_iter_get_basic(&iterator, &value);
        return value ? value : "";
    }
    void Send(DBusMessage* message)
    {
        dbus_connection_send(connection_, message, nullptr);
        dbus_connection_flush(connection_);
        dbus_message_unref(message);
    }
    void Handle(DBusMessage* call)
    {
        if (!dbus_message_has_interface(call, "org.freedesktop.portal.FileChooser"))
            return;
        Request request;
        request.method = dbus_message_get_member(call);
        DBusMessageIter args;
        dbus_message_iter_init(call, &args);
        dbus_message_iter_next(&args);  // Parent identifier is deliberately empty without xdg-foreign.
        request.title = String(args);
        dbus_message_iter_next(&args);
        DBusMessageIter options;
        dbus_message_iter_recurse(&args, &options);
        while (dbus_message_iter_get_arg_type(&options) == DBUS_TYPE_DICT_ENTRY)
        {
            DBusMessageIter entry, value;
            dbus_message_iter_recurse(&options, &entry);
            const auto key = String(entry);
            dbus_message_iter_next(&entry);
            dbus_message_iter_recurse(&entry, &value);
            if (key == "multiple")
            {
                dbus_bool_t multiple{};
                dbus_message_iter_get_basic(&value, &multiple);
                request.multiple = multiple;
            }
            else if (key == "current_name")
                request.currentName = String(value);
            else if (key == "current_filter")
            {
                DBusMessageIter filter;
                dbus_message_iter_recurse(&value, &filter);
                request.selectedFilter = String(filter);
                dbus_message_iter_next(&filter);
                DBusMessageIter patterns;
                dbus_message_iter_recurse(&filter, &patterns);
                while (dbus_message_iter_get_arg_type(&patterns) == DBUS_TYPE_STRUCT)
                {
                    DBusMessageIter pattern;
                    dbus_message_iter_recurse(&patterns, &pattern);
                    dbus_message_iter_next(&pattern);
                    request.selectedPatterns.push_back(String(pattern));
                    dbus_message_iter_next(&patterns);
                }
            }
            else if (key == "filters")
            {
                DBusMessageIter filters;
                dbus_message_iter_recurse(&value, &filters);
                while (dbus_message_iter_get_arg_type(&filters) != DBUS_TYPE_INVALID)
                {
                    ++request.filters;
                    dbus_message_iter_next(&filters);
                }
            }
            dbus_message_iter_next(&options);
        }
        {
            const std::scoped_lock lock(mutex_);
            last_ = request;
        }
        const char* path = "/org/freedesktop/portal/desktop/request/test/selection";
        auto* reply = dbus_message_new_method_return(call);
        dbus_message_append_args(reply, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID);
        Send(reply);
        const auto response = response_.load();
        if (response == Response::OwnerClose)
            return;
        auto* signal = dbus_message_new_signal(path, "org.freedesktop.portal.Request", "Response");
        dbus_message_set_destination(signal, dbus_message_get_sender(call));
        DBusMessageIter body, entries;
        dbus_message_iter_init_append(signal, &body);
        uint32_t code = response == Response::Accept ? 0 : response == Response::Cancel ? 1 : 2;
        dbus_message_iter_append_basic(&body, DBUS_TYPE_UINT32, &code);
        dbus_message_iter_open_container(&body, DBUS_TYPE_ARRAY, "{sv}", &entries);
        if (code == 0)
        {
            DBusMessageIter entry, variant, uris;
            const char* key = "uris";
            dbus_message_iter_open_container(&entries, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
            dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
            dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "as", &variant);
            dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "s", &uris);
            const char* first = request.method == "SaveFile" ? "file:///tmp/lws-portal-saved"
                                                             : "file:///tmp/lws%20portal.png";
            dbus_message_iter_append_basic(&uris, DBUS_TYPE_STRING, &first);
            if (request.multiple)
            {
                const char* second = "file:///tmp/lws-portal-second.jpg";
                dbus_message_iter_append_basic(&uris, DBUS_TYPE_STRING, &second);
            }
            dbus_message_iter_close_container(&variant, &uris);
            dbus_message_iter_close_container(&entry, &variant);
            dbus_message_iter_close_container(&entries, &entry);
        }
        dbus_message_iter_close_container(&body, &entries);
        Send(signal);
    }
    DBusConnection* connection_{};
    std::jthread thread_;
    std::atomic<Response> response_{Response::Accept};
    std::mutex mutex_;
    Request last_;
};
