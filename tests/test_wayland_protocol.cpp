#if defined(LWS_TEST_WAYLAND_SERVER)
    #include <catch2/catch_test_macros.hpp>
    #include <LWS/Platform.hpp>
    #include <LWS/Window.hpp>
    #include <LWS/Wayland/WindowExtensions.hpp>
    #include <wayland-server.h>
    #include <wayland-client.h>
    #include <test-XDG_SHELL-server.h>
    #include <test-FRACTIONAL_SCALE-server.h>
    #include <test-VIEWPORTER-server.h>
    #include <algorithm>
    #include <array>
    #include <cstdlib>
    #include <functional>
    #include <future>
    #include <map>
    #include <memory>
    #include <mutex>
    #include <optional>
    #include <string>
    #include <thread>
    #include <vector>
    #include <sys/eventfd.h>
    #include <fcntl.h>
    #include <unistd.h>

namespace
{
    // All protocol objects are accessed on the server thread. Commands use the server event loop, not test-only
    // access to client internals; assertions observe public LWS state and actual requests sent over the socket.
    class ProtocolServer
    {
      public:

        struct Seat
        {
            ProtocolServer* server;
            wl_global* global{};
            std::vector<wl_resource*> bindings, pointers, keyboards;
            uint32_t capabilities{WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD};
            unsigned bindCount{};
        };
        struct Surface
        {
            ProtocolServer* server;
            wl_resource* surface{};
            wl_resource* shell{};
            wl_resource* toplevel{};
            wl_resource* fractional{};
            wl_resource* buffer{};
            int bufferScale{1};
            bool configured{};
            bool holdBuffers{};
            std::vector<wl_resource*> heldBuffers;
            std::vector<uint32_t> submittedBuffers;
        };
        ProtocolServer(bool fractional = false, bool hostFrame = false)
        {
            if (const auto* value = std::getenv("WAYLAND_DISPLAY"))
                oldDisplay_ = value;
            if (const auto* value = std::getenv("WAYLAND_SOCKET"))
                oldSocket_ = value;
            std::array<char, 64> path{};
            std::string pattern = "/tmp/lws-protocol-XXXXXX";
            std::copy(pattern.begin(), pattern.end(), path.begin());
            const char* directory = mkdtemp(path.data());
            if (!directory)
                throw std::runtime_error("mkdtemp");
            directory_ = directory;
            socket_ = directory_ + "/display";
            display_ = wl_display_create();
            if (!display_ || wl_display_add_socket(display_, socket_.c_str()) != 0)
                throw std::runtime_error("private Wayland display");
            wl_display_init_shm(display_);
            wl_global_create(display_, &wl_compositor_interface, 4, this, BindCompositor);
            wl_global_create(display_, &xdg_wm_base_interface, 1, this, BindShell);
            wl_global_create(display_, &wl_output_interface, 2, this, BindOutput);
            if (hostFrame)
            {
                // LWS recognizes this advertised host-frame capability without binding the private interface.
                static const wl_interface hostInterface{"weston_rdprail_shell", 1, 0, nullptr, 0, nullptr};
                wl_global_create(display_, &hostInterface, 1, this, [](wl_client*, void*, uint32_t, uint32_t) {});
                wl_global_create(display_, &wl_subcompositor_interface, 1, this, BindSubcompositor);
            }
            if (fractional)
            {
                wl_global_create(display_, &wp_fractional_scale_manager_v1_interface, 1, this, BindFractional);
                wl_global_create(display_, &wp_viewporter_interface, 1, this, BindViewporter);
            }
            AddSeat();
            AddSeat();
            wake_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
            if (wake_ < 0)
                throw std::runtime_error("eventfd");
            source_ = wl_event_loop_add_fd(wl_display_get_event_loop(display_), wake_, WL_EVENT_READABLE, Commands,
                                           this);
            unsetenv("WAYLAND_SOCKET");
            setenv("WAYLAND_DISPLAY", socket_.c_str(), 1);
            thread_ = std::thread([this] { wl_display_run(display_); });
        }
        ~ProtocolServer()
        {
            Invoke([&] { wl_display_terminate(display_); });
            thread_.join();
            wl_event_source_remove(source_);
            close(wake_);
            wl_display_destroy_clients(display_);
            wl_display_destroy(display_);
            if (oldDisplay_)
                setenv("WAYLAND_DISPLAY", oldDisplay_->c_str(), 1);
            else
                unsetenv("WAYLAND_DISPLAY");
            if (oldSocket_)
                setenv("WAYLAND_SOCKET", oldSocket_->c_str(), 1);
            else
                unsetenv("WAYLAND_SOCKET");
            rmdir(directory_.c_str());
        }
        void Invoke(std::function<void()> operation)
        {
            auto task = std::make_shared<std::packaged_task<void()>>(std::move(operation));
            auto done = task->get_future();
            {
                const std::scoped_lock lock(mutex_);
                commands_.push_back([task] { (*task)(); });
            }
            const uint64_t value = 1;
            if (write(wake_, &value, sizeof(value)) != sizeof(value))
                throw std::runtime_error("server wake");
            done.get();
        }
        void Settle(LWS::PlatformContext& context)
        {
            // Registry -> seat capabilities -> pointer/keyboard creation may require separate roundtrips.
            for (int i = 0; i < 3; ++i)
                REQUIRE(context.RefreshMonitors() == LWS::Result::Success);
            Invoke([] {});
        }
        void AddSeat()
        {
            auto seat = std::make_unique<Seat>();
            seat->server = this;
            seat->global = wl_global_create(display_, &wl_seat_interface, 5, seat.get(), BindSeat);
            seats.push_back(std::move(seat));
        }
        void Capabilities(size_t index, uint32_t capabilities)
        {
            auto& seat = *seats.at(index);
            seat.capabilities = capabilities;
            for (auto* resource : seat.bindings)
                wl_seat_send_capabilities(resource, capabilities);
        }
        void RemoveSeat(size_t index)
        {
            auto& seat = *seats.at(index);
            wl_global_destroy(seat.global);
            seat.global = nullptr;
        }
        void Enter(size_t seat, uint32_t surface, uint32_t serial)
        {
            wl_pointer_send_enter(seats.at(seat)->pointers.at(0), serial, surfaces.at(surface)->surface,
                                  wl_fixed_from_int(10), wl_fixed_from_int(12));
        }
        void Leave(size_t seat, uint32_t surface, uint32_t serial)
        {
            wl_pointer_send_leave(seats.at(seat)->pointers.at(0), serial, surfaces.at(surface)->surface);
        }
        void EnterOutput(uint32_t surface) { wl_surface_send_enter(surfaces.at(surface)->surface, outputs_.at(0)); }
        void Scale(int scale)
        {
            for (auto* output : outputs_)
            {
                wl_output_send_scale(output, scale);
                wl_output_send_done(output);
            }
        }
        void Fractional(uint32_t surface, uint32_t scale)
        {
            wp_fractional_scale_v1_send_preferred_scale(surfaces.at(surface)->fractional, scale);
        }
        std::vector<std::unique_ptr<Seat>> seats;
        std::map<uint32_t, std::unique_ptr<Surface>> surfaces;
        unsigned cursorRequests{};
        uint32_t cursorSerial{};
        bool cursorHidden{};

      private:

        static void Destroy(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }
        static int Commands(int fd, uint32_t, void* data)
        {
            auto& self = *static_cast<ProtocolServer*>(data);
            uint64_t value{};
            std::ignore = read(fd, &value, sizeof(value));
            std::vector<std::function<void()>> work;
            {
                const std::scoped_lock lock(self.mutex_);
                work.swap(self.commands_);
            }
            for (auto& operation : work)
                operation();
            wl_display_flush_clients(self.display_);
            return 0;
        }
        static void BindSeat(wl_client* client, void* data, uint32_t version, uint32_t id)
        {
            auto& seat = *static_cast<Seat*>(data);
            auto* resource = wl_resource_create(client, &wl_seat_interface, version, id);
            static const struct wl_seat_interface implementation{
                .get_pointer =
                    [](wl_client* client, wl_resource* seatResource, uint32_t id)
                {
                    auto& seat = *static_cast<Seat*>(wl_resource_get_user_data(seatResource));
                    auto* pointer = wl_resource_create(client, &wl_pointer_interface,
                                                       wl_resource_get_version(seatResource), id);
                    static const struct wl_pointer_interface impl{
                        .set_cursor =
                            [](wl_client*, wl_resource* resource, uint32_t serial, wl_resource* surface, int32_t,
                               int32_t)
                        {
                            auto& server = *static_cast<Seat*>(wl_resource_get_user_data(resource))->server;
                            ++server.cursorRequests;
                            server.cursorSerial = serial;
                            server.cursorHidden = surface == nullptr;
                        },
                        .release = Destroy};
                    seat.pointers.push_back(pointer);
                    wl_resource_set_implementation(pointer, &impl, &seat,
                                                   [](wl_resource* resource)
                                                   {
                                                       auto& seat = *static_cast<Seat*>(
                                                           wl_resource_get_user_data(resource));
                                                       std::erase(seat.pointers, resource);
                                                   });
                },
                .get_keyboard =
                    [](wl_client* client, wl_resource* seatResource, uint32_t id)
                {
                    auto& seat = *static_cast<Seat*>(wl_resource_get_user_data(seatResource));
                    auto* keyboard = wl_resource_create(client, &wl_keyboard_interface,
                                                        wl_resource_get_version(seatResource), id);
                    static const struct wl_keyboard_interface impl{.release = Destroy};
                    // LWS exposes physical key codes; advertise the protocol's valid no-keymap mode.
                    const int keymap = open("/dev/null", O_RDONLY | O_CLOEXEC);
                    if (keymap >= 0)
                    {
                        wl_keyboard_send_keymap(keyboard, WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP, keymap, 0);
                        close(keymap);
                    }
                    seat.keyboards.push_back(keyboard);
                    wl_resource_set_implementation(keyboard, &impl, &seat,
                                                   [](wl_resource* resource)
                                                   {
                                                       auto& seat = *static_cast<Seat*>(
                                                           wl_resource_get_user_data(resource));
                                                       std::erase(seat.keyboards, resource);
                                                   });
                },
                .get_touch = nullptr,
                .release = Destroy};
            seat.bindings.push_back(resource);
            ++seat.bindCount;
            wl_resource_set_implementation(resource, &implementation, &seat,
                                           [](wl_resource* resource)
                                           {
                                               auto& seat = *static_cast<Seat*>(wl_resource_get_user_data(resource));
                                               std::erase(seat.bindings, resource);
                                           });
            wl_seat_send_capabilities(resource, seat.capabilities);
            wl_seat_send_name(resource, "test-seat");
        }
        static void BindSubcompositor(wl_client* client, void*, uint32_t version, uint32_t id)
        {
            auto* resource = wl_resource_create(client, &wl_subcompositor_interface, version, id);
            static const struct wl_subcompositor_interface impl{
                .destroy = Destroy,
                .get_subsurface = [](wl_client* client, wl_resource*, uint32_t id, wl_resource*, wl_resource*)
                {
                    auto* resource = wl_resource_create(client, &wl_subsurface_interface, 1, id);
                    static const struct wl_subsurface_interface impl{
                        .destroy = Destroy,
                        .set_position = [](wl_client*, wl_resource*, int32_t, int32_t) {},
                        .place_above = [](wl_client*, wl_resource*, wl_resource*) {},
                        .place_below = [](wl_client*, wl_resource*, wl_resource*) {},
                        .set_sync = [](wl_client*, wl_resource*) {},
                        .set_desync = [](wl_client*, wl_resource*) {}};
                    wl_resource_set_implementation(resource, &impl, nullptr, nullptr);
                }};
            wl_resource_set_implementation(resource, &impl, nullptr, nullptr);
        }
        static void BindCompositor(wl_client* client, void* data, uint32_t version, uint32_t id)
        {
            auto* resource = wl_resource_create(client, &wl_compositor_interface, version, id);
            static const struct wl_compositor_interface implementation{
                .create_surface =
                    [](wl_client* client, wl_resource* compositor, uint32_t id)
                {
                    auto& self = *static_cast<ProtocolServer*>(wl_resource_get_user_data(compositor));
                    auto surface = std::make_unique<Surface>();
                    surface->server = &self;
                    surface->surface = wl_resource_create(client, &wl_surface_interface,
                                                          wl_resource_get_version(compositor), id);
                    static const struct wl_surface_interface impl{
                        .destroy = Destroy,
                        .attach = [](wl_client*, wl_resource* resource, wl_resource* buffer, int32_t, int32_t)
                        { static_cast<Surface*>(wl_resource_get_user_data(resource))->buffer = buffer; },
                        .damage = [](wl_client*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {},
                        .frame =
                            [](wl_client* client, wl_resource*, uint32_t id)
                        {
                            auto* frame = wl_resource_create(client, &wl_callback_interface, 1, id);
                            wl_callback_send_done(frame, 1);
                            wl_resource_destroy(frame);
                        },
                        .set_opaque_region = [](wl_client*, wl_resource*, wl_resource*) {},
                        .set_input_region = [](wl_client*, wl_resource*, wl_resource*) {},
                        .commit =
                            [](wl_client*, wl_resource* resource)
                        {
                            auto& surface = *static_cast<Surface*>(wl_resource_get_user_data(resource));
                            if (surface.toplevel && !surface.configured)
                            {
                                surface.configured = true;
                                wl_array states{};
                                xdg_toplevel_send_configure(surface.toplevel, 0, 0, &states);
                                xdg_surface_send_configure(surface.shell, 1);
                            }
                            if (surface.buffer)
                            {
                                surface.submittedBuffers.push_back(wl_resource_get_id(surface.buffer));
                                if (surface.holdBuffers)
                                    surface.heldBuffers.push_back(surface.buffer);
                                else
                                    wl_buffer_send_release(surface.buffer);
                                surface.buffer = nullptr;
                            }
                        },
                        .set_buffer_transform = [](wl_client*, wl_resource*, int32_t) {},
                        .set_buffer_scale = [](wl_client*, wl_resource* resource, int32_t scale)
                        { static_cast<Surface*>(wl_resource_get_user_data(resource))->bufferScale = scale; },
                        .damage_buffer = [](wl_client*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {}};
                    wl_resource_set_implementation(surface->surface, &impl, surface.get(),
                                                   [](wl_resource* resource)
                                                   {
                                                       auto& surface = *static_cast<Surface*>(
                                                           wl_resource_get_user_data(resource));
                                                       surface.server->surfaces.erase(wl_resource_get_id(resource));
                                                   });
                    self.surfaces.emplace(id, std::move(surface));
                },
                .create_region =
                    [](wl_client* client, wl_resource*, uint32_t id)
                {
                    auto* region = wl_resource_create(client, &wl_region_interface, 1, id);
                    static const struct wl_region_interface impl{
                        .destroy = Destroy,
                        .add = [](wl_client*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {},
                        .subtract = [](wl_client*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {}};
                    wl_resource_set_implementation(region, &impl, nullptr, nullptr);
                }};
            wl_resource_set_implementation(resource, &implementation, data, nullptr);
        }
        static void BindShell(wl_client* client, void* data, uint32_t version, uint32_t id)
        {
            auto* resource = wl_resource_create(client, &xdg_wm_base_interface, version, id);
            static const struct xdg_wm_base_interface impl{
                .destroy = Destroy,
                .create_positioner = nullptr,
                .get_xdg_surface =
                    [](wl_client* client, wl_resource*, uint32_t id, wl_resource* native)
                {
                    auto& surface = *static_cast<Surface*>(wl_resource_get_user_data(native));
                    surface.shell = wl_resource_create(client, &xdg_surface_interface, 1, id);
                    static const struct xdg_surface_interface shell{
                        .destroy = Destroy,
                        .get_toplevel =
                            [](wl_client* client, wl_resource* resource, uint32_t id)
                        {
                            auto& surface = *static_cast<Surface*>(wl_resource_get_user_data(resource));
                            surface.toplevel = wl_resource_create(client, &xdg_toplevel_interface, 1, id);
                            static const struct xdg_toplevel_interface top{
                                .destroy = Destroy,
                                .set_parent = [](wl_client*, wl_resource*, wl_resource*) {},
                                .set_title = [](wl_client*, wl_resource*, const char*) {},
                                .set_app_id = [](wl_client*, wl_resource*, const char*) {},
                                .show_window_menu = nullptr,
                                .move = nullptr,
                                .resize = nullptr,
                                .set_max_size = [](wl_client*, wl_resource*, int32_t, int32_t) {},
                                .set_min_size = [](wl_client*, wl_resource*, int32_t, int32_t) {},
                                .set_maximized = [](wl_client*, wl_resource*) {},
                                .unset_maximized = [](wl_client*, wl_resource*) {},
                                .set_fullscreen = [](wl_client*, wl_resource*, wl_resource*) {},
                                .unset_fullscreen = [](wl_client*, wl_resource*) {},
                                .set_minimized = [](wl_client*, wl_resource*) {}};
                            wl_resource_set_implementation(surface.toplevel, &top, &surface, nullptr);
                        },
                        .get_popup = nullptr,
                        .set_window_geometry = [](wl_client*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {},
                        .ack_configure = [](wl_client*, wl_resource*, uint32_t) {}};
                    wl_resource_set_implementation(surface.shell, &shell, &surface, nullptr);
                },
                .pong = [](wl_client*, wl_resource*, uint32_t) {}};
            wl_resource_set_implementation(resource, &impl, data, nullptr);
        }
        static void BindOutput(wl_client* client, void* data, uint32_t version, uint32_t id)
        {
            auto& self = *static_cast<ProtocolServer*>(data);
            auto* output = wl_resource_create(client, &wl_output_interface, version, id);
            static const struct wl_output_interface impl{.release = Destroy};
            wl_resource_set_implementation(output, &impl, &self, nullptr);
            self.outputs_.push_back(output);
            wl_output_send_geometry(output, 0, 0, 300, 200, WL_OUTPUT_SUBPIXEL_UNKNOWN, "LWS", "test",
                                    WL_OUTPUT_TRANSFORM_NORMAL);
            wl_output_send_mode(output, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED, 1280, 720, 60000);
            wl_output_send_scale(output, 1);
            wl_output_send_done(output);
        }
        static void BindFractional(wl_client* client, void*, uint32_t version, uint32_t id)
        {
            auto* manager = wl_resource_create(client, &wp_fractional_scale_manager_v1_interface, version, id);
            static const struct wp_fractional_scale_manager_v1_interface impl{
                .destroy = Destroy,
                .get_fractional_scale = [](wl_client* client, wl_resource*, uint32_t id, wl_resource* resource)
                {
                    auto& surface = *static_cast<Surface*>(wl_resource_get_user_data(resource));
                    surface.fractional = wl_resource_create(client, &wp_fractional_scale_v1_interface, 1, id);
                    static const struct wp_fractional_scale_v1_interface scale{.destroy = Destroy};
                    wl_resource_set_implementation(surface.fractional, &scale, &surface, nullptr);
                }};
            wl_resource_set_implementation(manager, &impl, nullptr, nullptr);
        }
        static void BindViewporter(wl_client* client, void*, uint32_t version, uint32_t id)
        {
            auto* manager = wl_resource_create(client, &wp_viewporter_interface, version, id);
            static const struct wp_viewporter_interface impl{
                .destroy = Destroy,
                .get_viewport = [](wl_client* client, wl_resource*, uint32_t id, wl_resource*)
                {
                    auto* viewport = wl_resource_create(client, &wp_viewport_interface, 1, id);
                    static const struct wp_viewport_interface viewportImpl{
                        .destroy = Destroy,
                        .set_source = [](wl_client*, wl_resource*, wl_fixed_t, wl_fixed_t, wl_fixed_t, wl_fixed_t) {},
                        .set_destination = [](wl_client*, wl_resource*, int32_t, int32_t) {}};
                    wl_resource_set_implementation(viewport, &viewportImpl, nullptr, nullptr);
                }};
            wl_resource_set_implementation(manager, &impl, nullptr, nullptr);
        }
        std::optional<std::string> oldDisplay_, oldSocket_;
        std::string directory_, socket_;
        wl_display* display_{};
        std::vector<wl_resource*> outputs_;
        int wake_{-1};
        wl_event_source* source_{};
        std::mutex mutex_;
        std::vector<std::function<void()>> commands_;
        std::thread thread_;
    };

    LWS::WindowConfig Config()
    {
        return {.clientSize = {101, 51},
                .styles = LWS::WindowStyleFlags(LWS::WindowStyle::NoStyle),
                .eraseBackground = false};
    }
}  // namespace

TEST_CASE("Wayland seat capability loss and deterministic promotion use real protocol events",
          "[wayland][protocol][seat]")
{
    ProtocolServer server;
    LWS::PlatformContext context;
    REQUIRE(context.Init({.backend = LWS::BackendId::Wayland}) == LWS::Result::Success);
    server.Settle(context);
    size_t firstPointers{}, secondBindings{};
    server.Invoke(
        [&]
        {
            firstPointers = server.seats[0]->pointers.size();
            secondBindings = server.seats[1]->bindings.size();
        });
    REQUIRE(firstPointers == 1);
    REQUIRE(secondBindings == 0);
    server.Invoke([&] { server.Capabilities(0, 0); });
    server.Settle(context);
    size_t devices{};
    server.Invoke([&] { devices = server.seats[0]->pointers.size() + server.seats[0]->keyboards.size(); });
    REQUIRE(devices == 0);
    REQUIRE(context.IsUsable());
    server.Invoke([&] { server.Capabilities(0, WL_SEAT_CAPABILITY_POINTER); });
    server.Settle(context);
    server.Invoke([&] { devices = server.seats[0]->pointers.size(); });
    REQUIRE(devices == 1);
    server.Invoke([&] { server.RemoveSeat(0); });
    server.Settle(context);
    server.Invoke(
        [&]
        {
            devices = server.seats[1]->pointers.size();
            secondBindings = server.seats[1]->bindCount;
        });
    REQUIRE(devices == 1);
    REQUIRE(secondBindings == 1);
    server.Invoke([&] { server.RemoveSeat(1); });
    server.Settle(context);
    REQUIRE(context.IsUsable());
    REQUIRE_FALSE(context.IsKeyPressed(LWS::KeyCode::A).value());
    server.Invoke([&] { server.AddSeat(); });
    server.Settle(context);
    server.Invoke([&] { devices = server.seats[2]->pointers.size(); });
    REQUIRE(devices == 1);
}

TEST_CASE("Wayland cursor selection stays local through focus and capability transitions",
          "[wayland][protocol][cursor]")
{
    ProtocolServer server;
    LWS::PlatformContext context;
    REQUIRE(context.Init({.backend = LWS::BackendId::Wayland}) == LWS::Result::Success);
    LWS::Window first(context), second(context);
    REQUIRE(first.Create(Config()) == LWS::Result::Success);
    REQUIRE(second.Create(Config()) == LWS::Result::Success);
    server.Settle(context);
    const auto a = wl_proxy_get_id(reinterpret_cast<wl_proxy*>(*LWS::Wayland::GetSurface(first)));
    const auto b = wl_proxy_get_id(reinterpret_cast<wl_proxy*>(*LWS::Wayland::GetSurface(second)));
    const auto cursor = LWS::Cursor::FromShape(LWS::CursorShape::Hand);
    REQUIRE(first.SetMouseCursor(cursor) == LWS::Result::Success);
    REQUIRE(second.SetMouseCursor(cursor) == LWS::Result::Success);
    server.Invoke([&] { server.Enter(0, a, 11); });
    server.Settle(context);
    REQUIRE(first.IsMouseInClientRect());
    server.Invoke(
        [&]
        {
            server.Leave(0, a, 12);
            server.Enter(0, b, 13);
        });
    server.Settle(context);
    REQUIRE_FALSE(first.IsMouseInClientRect());
    REQUIRE(second.IsMouseInClientRect());
    server.Invoke(
        [&]
        {
            auto* keyboard = server.seats[0]->keyboards.at(0);
            wl_array keys{};
            wl_keyboard_send_enter(keyboard, 14, server.surfaces.at(b)->surface, &keys);
            wl_keyboard_send_key(keyboard, 15, 1, 30, WL_KEYBOARD_KEY_STATE_PRESSED);
        });
    server.Settle(context);
    REQUIRE(second.HasKeyboardFocus());
    REQUIRE(context.IsKeyPressed(LWS::KeyCode::A).value());
    unsigned before{}, after{};
    bool hidden{};
    uint32_t serial{};
    server.Invoke([&] { before = server.cursorRequests; });
    REQUIRE(first.SetMouseCursorVisible(false) == LWS::Result::Success);
    server.Settle(context);
    server.Invoke([&] { after = server.cursorRequests; });
    REQUIRE(after == before);
    REQUIRE(second.SetMouseCursorVisible(false) == LWS::Result::Success);
    server.Settle(context);
    server.Invoke(
        [&]
        {
            hidden = server.cursorHidden;
            serial = server.cursorSerial;
        });
    REQUIRE(hidden);
    REQUIRE(serial == 13);
    server.Invoke([&] { server.Capabilities(0, 0); });
    server.Settle(context);
    REQUIRE_FALSE(second.IsMouseInClientRect());
    REQUIRE_FALSE(second.HasKeyboardFocus());
    REQUIRE_FALSE(context.IsKeyPressed(LWS::KeyCode::A).value());
    server.Invoke([&] { before = server.cursorRequests; });
    REQUIRE(second.SetMouseCursorVisible(true) == LWS::Result::Success);
    server.Settle(context);
    server.Invoke([&] { after = server.cursorRequests; });
    REQUIRE(after == before);
    server.Invoke([&] { server.Capabilities(0, WL_SEAT_CAPABILITY_POINTER); });
    server.Settle(context);
    server.Invoke([&] { server.Enter(0, b, 99); });
    server.Settle(context);
    REQUIRE(second.IsMouseInClientRect());
    REQUIRE(second.SetMouseCursorVisible(false) == LWS::Result::Success);
    server.Settle(context);
    server.Invoke([&] { serial = server.cursorSerial; });
    REQUIRE(serial == 99);
}

TEST_CASE("Wayland integer and fractional scaling preserve handles and coherent metrics", "[wayland][protocol][scale]")
{
    bool fractional = false;
    SECTION("integer") {}
    SECTION("fractional")
    {
        fractional = true;
    }
    ProtocolServer server(fractional);
    LWS::PlatformContext context;
    REQUIRE(context.Init({.backend = LWS::BackendId::Wayland}) == LWS::Result::Success);
    LWS::Window window(context);
    unsigned events{};
    auto listener = window.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventClientAreaSizeChanged>(event))
                ++events;
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(listener);
    REQUIRE(window.Create(Config()) == LWS::Result::Success);
    const auto surface = LWS::Wayland::GetSurface(window);
    server.Settle(context);
    REQUIRE(window.IsConfigured());
    const auto id = wl_proxy_get_id(reinterpret_cast<wl_proxy*>(*surface));
    server.Invoke(
        [&]
        {
            server.EnterOutput(id);
            server.Scale(2);
            if (fractional)
                server.Fractional(id, 240);
        });
    server.Settle(context);
    REQUIRE(window.GetClientAreaSize()->logical == LWS::LogicalSize{101, 51});
    REQUIRE(window.GetClientAreaSize()->pixels == LWS::PixelSize{202, 102});
    const auto before = events;
    server.Invoke([&] { server.Scale(2); });
    server.Settle(context);
    REQUIRE(events == before);
    if (fractional)
    {
        server.Invoke([&] { server.Fractional(id, 150); });
        server.Settle(context);
        REQUIRE(window.GetClientAreaSize()->pixels == LWS::PixelSize{127, 64});
        const auto scaled = events;
        server.Invoke(
            [&]
            {
                server.Fractional(id, 150);
                server.Scale(3);
            });
        server.Settle(context);
        REQUIRE(events == scaled);
        REQUIRE(window.GetClientAreaSize()->pixels == LWS::PixelSize{127, 64});
    }
    REQUIRE(LWS::Wayland::GetSurface(window) == surface);
    int bufferScale{};
    server.Invoke([&] { bufferScale = server.surfaces.at(id)->bufferScale; });
    REQUIRE(bufferScale == (fractional ? 1 : 2));
}
TEST_CASE("Wayland bitmap staging preserves borrowed pixels and compositor ownership",
          "[wayland][protocol][presentation]")
{
    ProtocolServer server;
    LWS::PlatformContext context;
    REQUIRE(context.Init({.backend = LWS::BackendId::Wayland}) == LWS::Result::Success);
    LWS::Window window(context);
    auto config = Config();
    config.visible = true;
    REQUIRE(window.Create(config) == LWS::Result::Success);
    server.Settle(context);
    REQUIRE(window.IsConfigured());
    const auto surface = LWS::Wayland::GetSurface(window);
    REQUIRE(surface);
    const auto id = wl_proxy_get_id(reinterpret_cast<wl_proxy*>(*surface));
    server.Invoke([&] { server.surfaces.at(id)->holdBuffers = true; });

    const auto present = [&](uint32_t color)
    {
        const auto size = window.GetClientAreaSize()->pixels;
        std::vector<uint32_t> pixels(static_cast<size_t>(size.x) * size.y, color);
        REQUIRE(window.PresentBitmap({.pixels = std::as_bytes(std::span(pixels)),
                                      .width = static_cast<uint32_t>(size.x),
                                      .height = static_cast<uint32_t>(size.y)}) == LWS::Result::Success);
        // Both submitted and pending frames must survive immediate source modification and destruction.
        std::ranges::fill(pixels, 0);
        server.Settle(context);
    };
    const auto release = [&]
    {
        server.Invoke(
            [&]
            {
                auto& held = server.surfaces.at(id)->heldBuffers;
                wl_buffer_send_release(held.at(0));
                held.erase(held.begin());
            });
        server.Settle(context);
    };
    const auto check = [&](std::vector<uint32_t> colors, int width = 101, int height = 51)
    {
        bool matches = true;
        server.Invoke(
            [&]
            {
                const auto& held = server.surfaces.at(id)->heldBuffers;
                matches = held.size() == colors.size() && held.size() <= 3;
                for (size_t i = 0; matches && i < held.size(); ++i)
                {
                    auto* buffer = wl_shm_buffer_get(held[i]);
                    matches = buffer && wl_shm_buffer_get_width(buffer) == width &&
                              wl_shm_buffer_get_height(buffer) == height;
                    if (!matches)
                        break;
                    wl_shm_buffer_begin_access(buffer);
                    const auto* pixels = static_cast<const uint32_t*>(wl_shm_buffer_get_data(buffer));
                    matches = std::all_of(pixels, pixels + static_cast<size_t>(width) * height,
                                          [&](uint32_t pixel) { return pixel == colors[i]; });
                    wl_shm_buffer_end_access(buffer);
                }
            });
        REQUIRE(matches);
    };
    present(0xff000001);
    present(0xff000002);
    present(0xff000003);
    present(0xff000004);
    present(0xff000005);
    check({0xff000001, 0xff000002, 0xff000003});

    SECTION("release submits only the newest pending frame and reuses released buffers")
    {
        // Rejection must not replace the previously accepted pending frame.
        REQUIRE(window.PresentBitmap({.width = 101, .height = 51}) == LWS::Result::Failure);
        release();
        check({0xff000002, 0xff000003, 0xff000005});
        std::vector<uint32_t> expected{0xff000002, 0xff000003, 0xff000005};
        for (uint32_t color = 0xff000006; color < 0xff00000c; ++color)
        {
            present(color);
            check(expected);
            release();
            expected.erase(expected.begin());
            expected.push_back(color);
            check(expected);
        }
        size_t uniqueBuffers{};
        server.Invoke(
            [&]
            {
                auto ids = server.surfaces.at(id)->submittedBuffers;
                std::ranges::sort(ids);
                uniqueBuffers = static_cast<size_t>(std::unique(ids.begin(), ids.end()) - ids.begin());
            });
        REQUIRE(uniqueBuffers == 4);
    }
    SECTION("scale change discards a pending frame with obsolete dimensions")
    {
        server.Invoke(
            [&]
            {
                server.EnterOutput(id);
                server.Scale(2);
            });
        server.Settle(context);
        REQUIRE(window.GetClientAreaSize()->pixels == LWS::PixelSize{202, 102});
        release();
        check({0xff000002, 0xff000003});
        release();
        release();
        present(0xff000006);
        check({0xff000006}, 202, 102);
    }
    SECTION("hide discards pending pixels before buffers are released")
    {
        REQUIRE(window.SetVisible(false) == LWS::Result::Success);
        server.Settle(context);
        release();
        check({0xff000002, 0xff000003});
    }
    SECTION("destruction releases a pending frame without submitting it") {}
    REQUIRE(window.Destroy() == LWS::Result::Success);
    server.Settle(context);
    bool removed{};
    server.Invoke([&] { removed = !server.surfaces.contains(id); });
    REQUIRE(removed);
    REQUIRE(context.IsUsable());
}
TEST_CASE("Wayland background and caption paints reuse only released buffers", "[wayland][protocol][reuse]")
{
    bool caption = false;
    SECTION("background") {}
    SECTION("detached caption")
    {
        caption = true;
    }
    ProtocolServer server(false, caption);
    LWS::PlatformContext context;
    REQUIRE(context.Init({.backend = LWS::BackendId::Wayland}) == LWS::Result::Success);
    LWS::Window window(context);
    auto config = Config();
    config.visible = true;
    config.eraseBackground = !caption;
    if (caption)
        config.styles = LWS::WindowStyleFlags(LWS::WindowStyle::Caption);
    REQUIRE(window.Create(config) == LWS::Result::Success);
    server.Settle(context);
    const auto mainId = wl_proxy_get_id(reinterpret_cast<wl_proxy*>(*LWS::Wayland::GetSurface(window)));
    uint32_t id = mainId;
    server.Invoke(
        [&]
        {
            if (caption)
                for (auto& [candidate, surface] : server.surfaces)
                    if (candidate != mainId)
                        id = candidate;
            server.surfaces.at(id)->holdBuffers = true;
            server.surfaces.at(id)->submittedBuffers.clear();
        });
    REQUIRE((id != mainId) == caption);
    const auto paint = [&](unsigned value)
    {
        if (caption)
            REQUIRE(window.SetTitle(std::to_string(value)) == LWS::Result::Success);
        else
            REQUIRE(window.SetBackgroundColor({static_cast<int>(value), 0, 0, 255}) == LWS::Result::Success);
        server.Settle(context);
    };
    const auto release = [&]
    {
        server.Invoke(
            [&]
            {
                auto& held = server.surfaces.at(id)->heldBuffers;
                wl_buffer_send_release(held.at(0));
                held.erase(held.begin());
            });
        server.Settle(context);
    };
    const auto snapshot = [&]
    {
        std::vector<uint64_t> hashes;
        server.Invoke(
            [&]
            {
                for (auto* resource : server.surfaces.at(id)->heldBuffers)
                {
                    auto* buffer = wl_shm_buffer_get(resource);
                    wl_shm_buffer_begin_access(buffer);
                    const auto* pixels = static_cast<const uint8_t*>(wl_shm_buffer_get_data(buffer));
                    uint64_t hash = 14695981039346656037ULL;
                    const auto bytes = static_cast<size_t>(wl_shm_buffer_get_stride(buffer)) *
                                       wl_shm_buffer_get_height(buffer);
                    for (size_t i = 0; i < bytes; ++i)
                        hash = (hash ^ pixels[i]) * 1099511628211ULL;
                    hashes.push_back(hash);
                    wl_shm_buffer_end_access(buffer);
                }
            });
        return hashes;
    };
    paint(1);
    if (caption)
        paint(2);
    const auto held = snapshot();
    REQUIRE(held.size() == (caption ? 2 : 1));
    paint(3);
    paint(4);
    REQUIRE(snapshot() == held);
    release();
    const auto latest = snapshot();
    REQUIRE(latest.size() == held.size());
    REQUIRE(latest.back() != held.back());
    if (caption)
        REQUIRE(latest.front() == held.back());
    // Repeat the same coalesced result, ensuring no old paint is published after the latest one.
    paint(3);
    paint(4);
    release();
    REQUIRE(snapshot().back() == latest.back());
    size_t uniqueBuffers{};
    server.Invoke(
        [&]
        {
            auto ids = server.surfaces.at(id)->submittedBuffers;
            std::ranges::sort(ids);
            uniqueBuffers = static_cast<size_t>(std::unique(ids.begin(), ids.end()) - ids.begin());
        });
    REQUIRE(uniqueBuffers == held.size());
    server.Invoke(
        [&]
        {
            server.EnterOutput(mainId);
            server.Scale(2);
        });
    server.Settle(context);
    release();
    int width{};
    server.Invoke([&]
                  { width = wl_shm_buffer_get_width(wl_shm_buffer_get(server.surfaces.at(id)->heldBuffers.back())); });
    REQUIRE(width == 202);
    paint(5);
    REQUIRE(window.SetVisible(false) == LWS::Result::Success);
    server.Settle(context);
    const auto beforeRelease = snapshot().size();
    release();
    REQUIRE(snapshot().size() == beforeRelease - 1);
    REQUIRE(window.Destroy() == LWS::Result::Success);
    server.Settle(context);
    REQUIRE(context.IsUsable());
}
#endif
