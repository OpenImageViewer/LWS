#include <LWS/Platform.hpp>
#include <LWS/Win32/Platform.hpp>
#include <LWS/Win32/WindowExtensions.hpp>
#include <LWSUI/Composites.hpp>
#include <LWSUI/UIHost.hpp>
#include <dwmapi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using Role = LWS::WindowSnappingRole;
    using Mode = LWS::WindowSnappingMode;
    using Style = LWS::WindowStyle;

    void Check(LWS::Result result, const char* message)
    {
        if (result != LWS::Result::Success)
            throw std::runtime_error(message);
    }
    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
    const char* RoleName(Role role)
    {
        return role == Role::Leader ? "Leader" : role == Role::Follower ? "Follower" : "Independent";
    }

    class SnappingDemo final
    {
        struct Surface
        {
            std::unique_ptr<LWS::Window> window;
            std::unique_ptr<LWSUI::UIHost> host;
            LWSUI::ConnectionSet connections;
            std::vector<LWS::EventConnection> listeners;
            LWSUI::CheckBox *windows{}, *screen{}, *onTop{}, *resizable{};
            LWSUI::ComboBox* role{};
            LWSUI::Label* status{};
            LWSUI::Button *release{}, *reset{};
            LWSUI::ScrollView* properties{};
        };
        LWS::PlatformContext& platform_;
        std::array<Surface, 4> surfaces_;
        std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
        bool refreshPending_ = false;
        bool failed_ = false;

        static Role DefaultRole(size_t index) { return index < 2 ? Role::Leader : Role::Follower; }
        static LWS::LogicalSize DefaultSize(size_t index)
        {
            return index < 2 ? LWS::LogicalSize{460, 360} : LWS::LogicalSize{280, 280};
        }

        void ScheduleRefresh()
        {
            if (refreshPending_)
                return;
            refreshPending_ = true;
            const auto weak = std::weak_ptr(alive_);
            Check(platform_.PostTask(
                      [this, weak]
                      {
                          if (const auto alive = weak.lock(); alive && *alive)
                          {
                              refreshPending_ = false;
                              Refresh();
                          }
                      }),
                  "Cannot schedule snapping controls refresh");
        }

        void ApplyMode(Surface& surface)
        {
            const auto mode = surface.windows->Value()
                                  ? (surface.screen->Value() ? Mode::ScreenEdgesAndOtherWindows : Mode::OtherWindows)
                                  : (surface.screen->Value() ? Mode::ScreenEdges : Mode::Disabled);
            Check(surface.window->SetSnappingMode(mode), "Cannot change snapping mode");
            ScheduleRefresh();
        }
        void ApplyResizable(Surface& surface, bool value)
        {
            auto styles = surface.window->GetWindowStyles();
            if (value)
                styles.set(Style::ResizableBorder | Style::MaximizeButton);
            else
                styles.clear(Style::ResizableBorder | Style::MaximizeButton);
            Check(surface.window->SetWindowStyles(styles), "Cannot change resize border");
            ScheduleRefresh();
        }

        void Build(Surface& surface, size_t index)
        {
            auto root = std::make_unique<LWSUI::DockPanel>();
            root->gap = 6;
            auto status = std::make_unique<LWSUI::Label>();
            status->wrap = true;
            surface.status = status.get();
            root->AddDocked(std::move(status), LWSUI::Dock::Top);
            auto properties = std::make_unique<LWSUI::StackPanel>();
            surface.windows = &properties->Emplace<LWSUI::CheckBox>("Snap to windows");
            surface.screen = &properties->Emplace<LWSUI::CheckBox>("Snap to screen edges");
            surface.onTop = &properties->Emplace<LWSUI::CheckBox>("Always on top");
            surface.resizable = &properties->Emplace<LWSUI::CheckBox>("Resizable");
            auto& roleRow = properties->Emplace<LWSUI::Grid>();
            roleRow.columns = {42, -1};
            roleRow.Emplace<LWSUI::Label>("Role");
            surface.role = &roleRow.Emplace<LWSUI::ComboBox>(std::vector<LWSUI::Choice>{{"Independent", "Independent"},
                                                                                        {"Leader", "Leader"},
                                                                                        {"Follower", "Follower"}});
            if (index < 2)
            {
                auto& instructions = properties->Emplace<LWSUI::Label>(
                    "Drag either role against the other to attach opposite edges. Leaders carry followers. "
                    "Dragging a follower pulls it free. Disable window snapping to opt out. Close any window to exit.");
                instructions.wrap = true;
            }
            auto scroll = std::make_unique<LWSUI::ScrollView>();
            surface.properties = scroll.get();
            scroll->SetContent(std::move(properties));
            root->AddDocked(std::move(scroll), LWSUI::Dock::Fill);
            auto actions = std::make_unique<LWSUI::StackPanel>();
            surface.release = &actions->Emplace<LWSUI::Button>();
            if (index < 2)
            {
                surface.reset = &actions->Emplace<LWSUI::Button>("Reset demo");
                surface.connections.Connect(surface.reset->OnClick, [this] { Reset(); });
            }
            root->AddDocked(std::move(actions), LWSUI::Dock::Bottom);
            auto panel = std::make_unique<LWSUI::Panel>(std::move(root));
            panel->padding = LWSUI::Insets{10};
            surface.host->SetRoot(std::move(panel));
            surface.connections.Connect(surface.windows->OnChange, [this, &surface](bool) { ApplyMode(surface); });
            surface.connections.Connect(surface.screen->OnChange, [this, &surface](bool) { ApplyMode(surface); });
            surface.connections.Connect(surface.onTop->OnChange,
                                        [this, &surface](bool value)
                                        {
                                            Check(surface.window->SetAlwaysOnTop(value),
                                                  "Cannot change always-on-top state");
                                            ScheduleRefresh();
                                        });
            surface.connections.Connect(surface.resizable->OnChange,
                                        [this, &surface](bool value) { ApplyResizable(surface, value); });
            surface.connections.Connect(surface.role->OnChange,
                                        [this, &surface](const std::string& value)
                                        {
                                            const auto role = value == "Leader"     ? Role::Leader
                                                              : value == "Follower" ? Role::Follower
                                                                                    : Role::Independent;
                                            Check(surface.window->SetSnappingRole(role),
                                                  "Cannot change attachment role");
                                            ScheduleRefresh();
                                        });
            surface.connections.Connect(surface.release->OnClick,
                                        [this, &surface]
                                        {
                                            Check(surface.window->ReleaseSnappingAttachments(),
                                                  "Cannot release attachment");
                                            ScheduleRefresh();
                                        });
        }

        void Refresh()
        {
            constexpr std::array names{"A", "B", "1", "2"};
            for (size_t index = 0; index < surfaces_.size(); ++index)
            {
                auto& surface = surfaces_[index];
                if (!surface.window->IsCreated())
                    continue;
                const auto state = surface.window->GetSnappingState();
                const auto mode = surface.window->GetSnappingMode();
                surface.windows->SetValue(mode == Mode::OtherWindows || mode == Mode::ScreenEdgesAndOtherWindows);
                surface.screen->SetValue(mode == Mode::ScreenEdges || mode == Mode::ScreenEdgesAndOtherWindows);
                surface.onTop->SetValue(surface.window->IsAlwaysOnTop());
                surface.resizable->SetValue(surface.window->GetWindowStyles().test(Style::ResizableBorder));
                surface.role->SetValue(RoleName(state.role));
                surface.status->SetText(state.role == Role::Leader ? "Followers: " + std::to_string(state.followerCount)
                                        : state.attachedToLeader   ? "Attached to a leader"
                                                                   : "Unattached");
                surface.release->SetText(state.role == Role::Leader ? "Release followers" : "Detach");
                surface.release->SetEnabled(state.attachedToLeader || state.followerCount != 0);
                const auto title = LWSUI::NativeText(std::string("Snapping / ") + RoleName(state.role) + " " +
                                                     names[index]);
                if (surface.window->GetTitle() != title)
                    Check(surface.window->SetTitle(title), "Cannot update window identity");
                surface.host->Update();
            }
        }

        void Arrange()
        {
            const auto monitor = platform_.GetPrimaryMonitor();
            Require(monitor.has_value(), "Cannot find the primary monitor");
            const auto origin = monitor->workRect.GetCorner(LLUtils::TopLeft);
            const double sx = std::max(1.0, monitor->contentScale.x), sy = std::max(1.0, monitor->contentScale.y);
            const int width = int(monitor->workRect.GetWidth() / sx), height = int(monitor->workRect.GetHeight() / sy);
            const int column = std::max(220, std::min(460, (width - 80) / 2));
            const int row = std::max(160, std::min(360, (height - 140) / 2));
            const int x = int(origin.x / sx) + std::max(16, (width - column * 2 - 32) / 2);
            const int y = int(origin.y / sy) + 16;
            for (size_t index = 0; index < surfaces_.size(); ++index)
            {
                auto size = DefaultSize(index);
                size.x = std::min(size.x, column);
                size.y = std::min(size.y, row);
                Check(surfaces_[index].window->RequestPlacement(
                          {.position = LWS::Point{x + int(index % 2) * (column + 32), y + (index < 2 ? 0 : row + 72)},
                           .clientSize = size}),
                      "Cannot arrange snapping demo");
                // Clamp actual outer rectangles, including native decorations, to the work area.
                const auto hwnd = LWS::Win32::GetHwnd(*surfaces_[index].window);
                Require(hwnd.has_value(), "Cannot get demo window handle");
                RECT rectangle{};
                Require(GetWindowRect(*hwnd, &rectangle), "Cannot read demo window geometry");
                const auto work = monitor->workRect;
                const auto corner = work.GetCorner(LLUtils::TopLeft);
                const int right = corner.x + work.GetWidth(), bottom = corner.y + work.GetHeight();
                const int px = std::max<LONG>(corner.x,
                                              std::min(rectangle.left, right - (rectangle.right - rectangle.left)));
                const int py = std::max<LONG>(corner.y,
                                              std::min(rectangle.top, bottom - (rectangle.bottom - rectangle.top)));
                Require(SetWindowPos(*hwnd, nullptr, px, py, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER),
                        "Cannot fit demo window to monitor");
            }
        }

        void Pump()
        {
            for (int count = 0; count < 5; ++count)
                Require(platform_.ProcessMessages() == LWS::LoopResult::Continue, "Cannot process demo messages");
            Refresh();
            Require(!failed_, "A demo host reported an error");
        }
        static HWND Hwnd(Surface& surface)
        {
            const auto handle = LWS::Win32::GetHwnd(*surface.window);
            Require(handle.has_value(), "Cannot get smoke window handle");
            return *handle;
        }
        static RECT Rectangle(Surface& surface)
        {
            RECT rectangle{};
            Require(GetWindowRect(Hwnd(surface), &rectangle), "Cannot read smoke geometry");
            return rectangle;
        }
        static RECT Frame(Surface& surface)
        {
            auto rectangle = Rectangle(surface);
            RECT frame{};
            if (SUCCEEDED(DwmGetWindowAttribute(Hwnd(surface), DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))))
                rectangle = frame;
            return rectangle;
        }
        static void Position(Surface& surface, LONG x, LONG y)
        {
            Require(SetWindowPos(Hwnd(surface), nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER),
                    "Cannot position smoke window");
        }
        bool Drag(Surface& moving, Surface& target)
        {
            const auto frame = Frame(moving), other = Frame(target);
            auto proposed = Rectangle(moving);
            OffsetRect(&proposed, other.right + 1 - frame.left, other.top + 30 - frame.top);
            SendMessageW(Hwnd(moving), WM_ENTERSIZEMOVE, 0, 0);
            auto snapped = proposed;
            SendMessageW(Hwnd(moving), WM_MOVING, 0, reinterpret_cast<LPARAM>(&snapped));
            Position(moving, snapped.left, snapped.top);
            SendMessageW(Hwnd(moving), WM_EXITSIZEMOVE, 0, 0);
            Pump();
            return !EqualRect(&snapped, &proposed);
        }
        void Toggle(LWSUI::CheckBox& control)
        {
            Require(control.Dispatch({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Space}),
                    "Cannot activate smoke checkbox");
            Pump();
        }

      public:

        explicit SnappingDemo(LWS::PlatformContext& platform) : platform_(platform)
        {
            const auto theme = LWSUI::MakeTheme();
            for (size_t index = 0; index < surfaces_.size(); ++index)
            {
                auto& surface = surfaces_[index];
                surface.window = std::make_unique<LWS::Window>(platform_);
                LWS::WindowConfig config;
                config.clientSize = DefaultSize(index);
                config.clientSizeLimits.minimum = {220, 160};
                config.styles = Style::Caption | Style::CloseButton | Style::MinimizeButton;
                if (index < 2)
                    config.styles |= Style::ResizableBorder | Style::MaximizeButton;
                config.snappingMode = Mode::ScreenEdgesAndOtherWindows;
                config.snappingRole = DefaultRole(index);
                config.backgroundColor = theme.background;
                config.eraseBackground = false;
                Check(surface.window->Create(config), "Cannot create snapping window");
                surface.host = std::make_unique<LWSUI::UIHost>(*surface.window, theme);
                surface.host->SetRedrawOnResize(true);
                surface.connections.Connect(surface.host->OnError,
                                            [this, weak = std::weak_ptr(alive_)](const std::string& error)
                                            {
                                                const auto alive = weak.lock();
                                                if (!alive || !*alive)
                                                    return;
                                                failed_ = true;
                                                OutputDebugStringA(("Snapping demo: " + error + "\n").c_str());
                                            });
                Build(surface, index);
                auto listener = surface.window->Listen(
                    [this, weak = std::weak_ptr(alive_)](const LWS::AnyEvent& event)
                    {
                        const auto alive = weak.lock();
                        if (!alive || !*alive)
                            return LWS::EventResponse::Unhandled;
                        if (std::holds_alternative<LWS::EventCloseRequested>(event))
                        {
                            platform_.RequestQuit();
                            return LWS::EventResponse::Handled;
                        }
                        if (std::holds_alternative<LWS::EventSnappingStateChanged>(event))
                            ScheduleRefresh();
                        return LWS::EventResponse::Unhandled;
                    });
                Require(listener.has_value(), "Cannot observe snapping window");
                surface.listeners.push_back(std::move(*listener));
            }
            Reset();
            for (auto& surface : surfaces_)
                Check(surface.window->SetVisible(true), "Cannot show snapping window");
            Refresh();
        }
        ~SnappingDemo() { *alive_ = false; }
        bool Failed() const { return failed_; }
        void Reset()
        {
            for (auto& surface : surfaces_)
                Check(surface.window->ReleaseSnappingAttachments(), "Cannot reset attachments");
            for (size_t index = 0; index < surfaces_.size(); ++index)
            {
                auto& surface = surfaces_[index];
                if (surface.window->GetShowState() != LWS::WindowShowState::Restored)
                    Check(surface.window->RequestShowState(LWS::WindowShowState::Restored),
                          "Cannot restore demo window");
                Check(surface.window->SetSnappingRole(DefaultRole(index)), "Cannot reset role");
                Check(surface.window->SetSnappingMode(Mode::ScreenEdgesAndOtherWindows), "Cannot reset snapping");
                Check(surface.window->SetAlwaysOnTop(false), "Cannot reset topmost state");
                ApplyResizable(surface, index < 2);
                surface.properties->SetOffset(0);
            }
            Arrange();
            Refresh();
        }
        void Smoke()
        {
            Pump();
            for (size_t index = 0; index < surfaces_.size(); ++index)
            {
                const auto& surface = surfaces_[index];
                Require(surface.window->GetSnappingState() == LWS::WindowSnappingState{DefaultRole(index)},
                        "Wrong initial roles");
                Require(surface.windows->Value() && surface.screen->Value() && !surface.onTop->Value(),
                        "Wrong initial checkboxes");
                Require(surface.resizable->Value() == (index < 2), "Wrong initial resize policy");
                Require(surface.release->Bounds().height > 0, "Release controls are not visible");
            }
            auto& a = surfaces_[0];
            auto& b = surfaces_[1];
            auto& follower = surfaces_[2];
            auto& other = surfaces_[3];
            for (size_t index = 0; index < surfaces_.size(); ++index)
                Position(surfaces_[index], -20000 + LONG(index) * 2000, -20000);
            Toggle(*follower.windows);
            Require(follower.window->GetSnappingMode() == Mode::ScreenEdges, "Window checkbox did not update mode");
            Require(!Drag(a, follower), "Screen-only follower attracted a leader");
            Require(a.window->GetSnappingState().followerCount == 0, "Disabled target attached");
            Toggle(*follower.screen);
            Require(follower.window->GetSnappingMode() == Mode::Disabled, "Screen checkbox did not update mode");
            Require(!Drag(a, follower), "Disabled follower attracted a leader");
            Toggle(*follower.windows);
            Require(follower.window->GetSnappingMode() == Mode::OtherWindows, "Window-only mode failed");
            Toggle(*follower.screen);
            Require(Drag(follower, a), "Follower drag did not snap");
            Require(follower.window->GetSnappingState().attachedToLeader &&
                        a.window->GetSnappingState().followerCount == 1,
                    "Follower did not attach to leader A");
            Require(follower.status->Text() == "Attached to a leader" && a.status->Text() == "Followers: 1",
                    "Attachment notifications did not refresh status");
            auto before = Rectangle(follower), leader = Rectangle(a);
            Position(a, leader.left + 75, leader.top + 50);
            Require(Rectangle(follower).left == before.left + 75 && Rectangle(follower).top == before.top + 50,
                    "Leader did not carry its follower");
            Require(Drag(follower, b), "Follower did not snap to leader B");
            Require(a.window->GetSnappingState().followerCount == 0 && b.window->GetSnappingState().followerCount == 1,
                    "Same-drag follower transfer failed");
            before = Rectangle(follower);
            leader = Rectangle(b);
            Position(b, leader.left + 75, leader.top + 50);
            Require(Rectangle(follower).left == before.left + 75, "New leader did not carry follower");
            Require(Drag(follower, b) && follower.window->GetSnappingState().attachedToLeader &&
                        b.window->GetSnappingState().followerCount == 1,
                    "Follower could not snap back to the same leader");
            SendMessageW(Hwnd(follower), WM_ENTERSIZEMOVE, 0, 0);
            auto away = Rectangle(follower);
            OffsetRect(&away, 800, 600);
            SendMessageW(Hwnd(follower), WM_MOVING, 0, reinterpret_cast<LPARAM>(&away));
            Position(follower, away.left, away.top);
            SendMessageW(Hwnd(follower), WM_EXITSIZEMOVE, 0, 0);
            Pump();
            Require(!follower.window->GetSnappingState().attachedToLeader, "Direct follower drag did not detach");
            Require(Drag(a, follower), "Leader drag did not attach");
            follower.release->OnClick.Raise();
            Pump();
            Require(!follower.window->GetSnappingState().attachedToLeader && follower.windows->Value(),
                    "Detach action failed");
            Require(Drag(a, follower), "Cannot attach for leader release check");
            a.release->OnClick.Raise();
            Pump();
            Require(a.window->GetSnappingState().followerCount == 0, "Release followers action failed");
            Toggle(*other.onTop);
            Require(other.window->IsAlwaysOnTop(), "Always-on-top checkbox failed");
            Toggle(*other.onTop);
            Toggle(*other.resizable);
            Require(other.window->GetWindowStyles().test(Style::ResizableBorder | Style::Caption | Style::CloseButton),
                    "Resize checkbox lost existing styles");
            Toggle(*other.resizable);
            const auto size = other.window->GetClientAreaMetrics().logical;
            Require(other.role->Dispatch({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Right}),
                    "Cannot change role choice");
            Pump();
            Require(other.window->GetSnappingState().role == Role::Independent,
                    "Role choice did not select Independent");
            Require(other.role->Dispatch({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Right}),
                    "Cannot change role choice");
            Pump();
            Require(other.window->GetSnappingState().role == Role::Leader &&
                        other.window->GetClientAreaMetrics().logical == size,
                    "Role choice changed size category");
            Check(other.window->RequestPlacement({.clientSize = LWS::LogicalSize{220, 160}}),
                  "Cannot reduce follower viewport");
            Pump();
            for (auto* control :
                 std::array<LWSUI::Control*, 5>{other.windows, other.screen, other.onTop, other.resizable, other.role})
            {
                other.properties->SetOffset(other.properties->Offset() + control->Bounds().y -
                                            other.properties->Bounds().y);
                Pump();
                const auto bounds = control->Bounds(), viewport = other.properties->Bounds();
                Require(bounds.y >= viewport.y - 0.1f &&
                            bounds.y + bounds.height <= viewport.y + viewport.height + 0.1f,
                        "A property control is unreachable in reduced viewport");
            }
            Require(other.release->Bounds().height > 0, "Release action disappeared in reduced viewport");
            Check(a.window->RequestShowState(LWS::WindowShowState::Maximized), "Cannot maximize reset scenario");
            Check(b.window->RequestShowState(LWS::WindowShowState::Minimized), "Cannot minimize reset scenario");
            a.reset->OnClick.Raise();
            Pump();
            for (size_t index = 0; index < surfaces_.size(); ++index)
            {
                const auto& surface = surfaces_[index];
                Require(surface.window->GetSnappingState() == LWS::WindowSnappingState{DefaultRole(index)} &&
                            surface.windows->Value() && surface.screen->Value() && !surface.onTop->Value() &&
                            surface.resizable->Value() == (index < 2),
                        "Reset did not restore all properties");
                Require(surface.window->GetShowState() == LWS::WindowShowState::Restored,
                        "Reset did not restore window show state");
            }
            // Leave a live attachment so shutdown exercises notification and subscription cleanup.
            Require(Drag(a, follower) && follower.window->GetSnappingState().attachedToLeader,
                    "Cannot form the shutdown attachment");
        }
    };
}  // namespace

int main(int argc, char** argv)
{
    const bool smoke = argc == 2 && std::string_view(argv[1]) == "--smoke";
    try
    {
        Require(argc == 1 || smoke, "Usage: LWSUISnappingDemo [--smoke]");
        Check(LWS::Win32::BootstrapProcess(), "Cannot initialize Windows DPI awareness");
        LWS::PlatformContext platform;
        Check(platform.Init({.backend = LWS::BackendId::Win32}), "Cannot initialize Windows platform");
        SnappingDemo demo(platform);
        if (smoke)
        {
            demo.Smoke();
            return 0;
        }
        return platform.RunMessageLoop() == LWS::LoopResult::Failed || demo.Failed() ? 1 : 0;
    }
    catch (const std::exception& error)
    {
        OutputDebugStringA((std::string("Snapping demo: ") + error.what() + "\n").c_str());
        if (smoke)
            std::cerr << error.what() << '\n';
        else
            MessageBoxW(nullptr, LWSUI::NativeText(error.what()).c_str(), L"LWSUI Snapping demo", MB_OK | MB_ICONERROR);
        return 1;
    }
}
