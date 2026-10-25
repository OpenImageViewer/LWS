#ifdef LWS_PLATFORM_WIN32

    #include <catch2/catch_test_macros.hpp>
    #include <LWS/Platform.hpp>
    #include <LWS/Win32/Platform.hpp>
    #include <LWS/Win32/WindowExtensions.hpp>
    #include <LWS/Window.hpp>
    #include <LWS/source/Win32/internal/WindowDragAnchor.hpp>
    #include <Windows.h>

namespace
{
    class SnapContext
    {
      public:

        SnapContext()
        {
            REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
            REQUIRE(platform.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
        }
        LWS::PlatformContext platform;
    };

    HWND Native(LWS::Window& window)
    {
        const auto handle = LWS::Win32::GetHwnd(window);
        REQUIRE(handle.has_value());
        return *handle;
    }

    RECT Moving(LWS::Window& window, RECT rectangle)
    {
        SendMessageW(Native(window), WM_MOVING, 0, reinterpret_cast<LPARAM>(&rectangle));
        return rectangle;
    }
    RECT Rectangle(LWS::Window& window)
    {
        RECT rectangle{};
        REQUIRE(GetWindowRect(Native(window), &rectangle));
        return rectangle;
    }

    void Position(LWS::Window& window, LONG x, LONG y)
    {
        REQUIRE(SetWindowPos(Native(window), nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER));
    }

    void Attach(LWS::Window& moving, LWS::Window& target, bool below = false)
    {
        const auto other = Rectangle(target);
        SendMessageW(Native(moving), WM_ENTERSIZEMOVE, 0, 0);
        auto proposed = Rectangle(moving);
        if (below)
            OffsetRect(&proposed, other.left + 30 - proposed.left, other.bottom + 1 - proposed.top);
        else
            OffsetRect(&proposed, other.right + 1 - proposed.left, other.top + 30 - proposed.top);
        const auto snapped = Moving(moving, proposed);
        if (below)
            REQUIRE(snapped.top == other.bottom);
        else
            REQUIRE(snapped.left == other.right);
        Position(moving, snapped.left, snapped.top);
        SendMessageW(Native(moving), WM_EXITSIZEMOVE, 0, 0);
    }

    void CreateSnapWindow(LWS::Window& window, LWS::WindowSnappingRole role, LONG x, LONG y)
    {
        REQUIRE(window.Create({.clientSize = {100, 100},
                               .snappingMode = LWS::WindowSnappingMode::OtherWindows,
                               .snappingRole = role}) == LWS::Result::Success);
        Position(window, x, y);
        REQUIRE(window.SetVisible(true) == LWS::Result::Success);
    }

}  // namespace

TEST_CASE("Window snapping mode validates configuration and lifetime", "[window][snapping]")
{
    SnapContext context;
    LWS::Window window(context.platform);
    using Mode = LWS::WindowSnappingMode;
    REQUIRE(window.GetSnappingMode() == Mode::Disabled);
    REQUIRE(window.SetSnappingMode(Mode::ScreenEdges) == LWS::Result::InvalidState);
    LWS::WindowConfig config;
    config.snappingMode = static_cast<Mode>(99);
    REQUIRE(window.Create(config) == LWS::Result::InvalidArgument);
    config.snappingMode = Mode::ScreenEdgesAndOtherWindows;
    REQUIRE(window.Create(config) == LWS::Result::Success);
    REQUIRE(window.GetSnappingMode() == config.snappingMode);
    REQUIRE(window.SetSnappingMode(static_cast<Mode>(99)) == LWS::Result::InvalidArgument);
    REQUIRE(window.GetSnappingMode() == config.snappingMode);
    LWS::Window child(context.platform);
    LWS::WindowConfig childConfig;
    childConfig.parent = &window;
    childConfig.snappingMode = Mode::OtherWindows;
    REQUIRE(child.Create(childConfig) == LWS::Result::NotSupported);
    childConfig.snappingMode = Mode::Disabled;
    REQUIRE(child.Create(childConfig) == LWS::Result::Success);
    REQUIRE(child.SetSnappingMode(Mode::OtherWindows) == LWS::Result::NotSupported);
    REQUIRE(window.Destroy() == LWS::Result::Success);
    REQUIRE(window.SetSnappingMode(Mode::Disabled) == LWS::Result::InvalidState);
}

TEST_CASE("Dragging snaps to all monitor work area edges without resizing", "[window][snapping]")
{
    SnapContext context;
    LWS::Window window(context.platform);
    REQUIRE(window.Create({.clientSize = {100, 100}, .snappingMode = LWS::WindowSnappingMode::ScreenEdges}) ==
            LWS::Result::Success);
    MONITORINFO monitor{sizeof(monitor)};
    REQUIRE(GetMonitorInfoW(MonitorFromWindow(Native(window), MONITOR_DEFAULTTONEAREST), &monitor));
    const auto work = monitor.rcWork;
    const LONG x = (work.left + work.right) / 2;
    const LONG y = (work.top + work.bottom) / 2;
    for (const auto rectangle :
         {RECT{work.left + 1, y, work.left + 101, y + 100}, RECT{work.right - 101, y, work.right - 1, y + 100},
          RECT{x, work.top + 1, x + 100, work.top + 101}, RECT{x, work.bottom - 101, x + 100, work.bottom - 1}})
    {
        const RECT snapped = Moving(window, rectangle);
        REQUIRE(snapped.right - snapped.left == 100);
        REQUIRE(snapped.bottom - snapped.top == 100);
        REQUIRE((snapped.left == work.left || snapped.right == work.right || snapped.top == work.top ||
                 snapped.bottom == work.bottom));
    }
    const RECT corner = Moving(window, {work.left + 1, work.top + 1, work.left + 101, work.top + 101});
    REQUIRE(corner.left == work.left);
    REQUIRE(corner.top == work.top);
    REQUIRE(window.SetSnappingMode(LWS::WindowSnappingMode::Disabled) == LWS::Result::Success);
    REQUIRE(Moving(window, {work.left + 1, y, work.left + 101, y + 100}).left == work.left + 1);
    REQUIRE(window.SetSnappingMode(LWS::WindowSnappingMode::ScreenEdges) == LWS::Result::Success);
    REQUIRE(Moving(window, {work.left + 100, y, work.left + 200, y + 100}).left == work.left + 100);
}

TEST_CASE("Dragging snaps to neighboring windows and ignores hidden or distant windows", "[window][snapping]")
{
    SnapContext context;
    LWS::Window target(context.platform);
    LWS::Window moving(context.platform);
    REQUIRE(target.Create({.clientSize = {200, 200}, .snappingMode = LWS::WindowSnappingMode::OtherWindows}) ==
            LWS::Result::Success);
    REQUIRE(moving.Create({.clientSize = {100, 100}, .snappingMode = LWS::WindowSnappingMode::OtherWindows}) ==
            LWS::Result::Success);
    REQUIRE(SetWindowPos(Native(target), nullptr, -20000, -20000, 200, 200, SWP_NOACTIVATE | SWP_NOZORDER));
    REQUIRE(target.SetVisible(true) == LWS::Result::Success);
    RECT targetRect{};
    REQUIRE(GetWindowRect(Native(target), &targetRect));
    const RECT proposed{targetRect.right + 1, targetRect.top + 30, targetRect.right + 101, targetRect.top + 130};
    const RECT snapped = Moving(moving, proposed);
    REQUIRE(snapped.left == targetRect.right);
    REQUIRE(snapped.top == proposed.top);
    REQUIRE(snapped.right - snapped.left == 100);
    REQUIRE(target.SetVisible(false) == LWS::Result::Success);
    REQUIRE(Moving(moving, proposed).left == proposed.left);
    REQUIRE(target.SetVisible(true) == LWS::Result::Success);
    RECT distant = proposed;
    OffsetRect(&distant, 0, 500);
    REQUIRE(Moving(moving, distant).left == distant.left);
    REQUIRE(moving.SetSnappingMode(LWS::WindowSnappingMode::ScreenEdges) == LWS::Result::Success);
    REQUIRE(Moving(moving, proposed).left == proposed.left);
    REQUIRE(moving.SetSnappingMode(LWS::WindowSnappingMode::ScreenEdgesAndOtherWindows) == LWS::Result::Success);
    REQUIRE(Moving(moving, proposed).left == targetRect.right);
}

using Role = LWS::WindowSnappingRole;
using Mode = LWS::WindowSnappingMode;

TEST_CASE("Directional snapping carries multiple followers and direct movement detaches one",
          "[window][snapping][attachments]")
{
    SnapContext context;
    LWS::Window leader(context.platform), first(context.platform), second(context.platform);
    CreateSnapWindow(leader, Role::Leader, -19000, -19000);
    CreateSnapWindow(first, Role::Follower, -20000, -20000);
    CreateSnapWindow(second, Role::Follower, -18000, -18000);
    Attach(leader, first);
    REQUIRE(first.GetSnappingState().attachedToLeader);
    Attach(leader, second, true);
    REQUIRE(leader.GetSnappingState().followerCount == 2);
    REQUIRE(second.GetSnappingState().attachedToLeader);
    const auto a = Rectangle(leader), b = Rectangle(first), c = Rectangle(second);
    Position(leader, a.left + 73, a.top + 51);
    REQUIRE(Rectangle(first).left == b.left + 73);
    REQUIRE(Rectangle(first).top == b.top + 51);
    REQUIRE(Rectangle(second).left == c.left + 73);
    REQUIRE(Rectangle(second).top == c.top + 51);
    REQUIRE(Rectangle(first).right - Rectangle(first).left == b.right - b.left);
    REQUIRE(leader.GetSnappingState().followerCount == 2);

    const auto before = Rectangle(first), stationaryLeader = Rectangle(leader), stationarySecond = Rectangle(second);
    bool remainsDetached = true;
    SECTION("Independent native placement")
    {
        Position(first, before.left + 100, before.top + 100);
    }
    SECTION("Library placement")
    {
        const auto placement = first.GetPlacement();
        REQUIRE(placement.position.has_value());
        REQUIRE(first.RequestPlacement({.position = LWS::Point{placement.position->x + 40,
                                                               placement.position->y + 20}}) == LWS::Result::Success);
    }
    SECTION("Direct follower drag can snap back and reattach to its leader")
    {
        SendMessageW(Native(first), WM_ENTERSIZEMOVE, 0, 0);
        RECT proposed = before;
        OffsetRect(&proposed, 1, 0);
        const auto detached = Moving(first, proposed);
        REQUIRE(detached.left == before.left);
        REQUIRE_FALSE(first.GetSnappingState().attachedToLeader);
        Position(first, detached.left, detached.top);
        SendMessageW(Native(first), WM_EXITSIZEMOVE, 0, 0);
        REQUIRE(Rectangle(first).left == before.left);
        remainsDetached = false;
    }
    REQUIRE(first.GetSnappingState().attachedToLeader == !remainsDetached);
    REQUIRE(leader.GetSnappingState().followerCount == (remainsDetached ? 1 : 2));
    REQUIRE(Rectangle(leader).left == stationaryLeader.left);
    REQUIRE(Rectangle(second).left == stationarySecond.left);
}

TEST_CASE("Either role can initiate an eligible leader and follower attachment", "[window][snapping][attachments]")
{
    SnapContext context;
    LWS::Window source(context.platform), target(context.platform);
    Role sourceRole = Role::Leader, targetRole = Role::Follower;
    bool eligible = true;
    SECTION("Leader and follower") {}
    SECTION("Leader and leader")
    {
        targetRole = Role::Leader;
        eligible = false;
    }
    SECTION("Follower initiates")
    {
        sourceRole = Role::Follower;
        targetRole = Role::Leader;
    }
    SECTION("Follower and follower")
    {
        sourceRole = Role::Follower;
        eligible = false;
    }
    SECTION("Independent initiates")
    {
        sourceRole = Role::Independent;
        eligible = false;
    }
    SECTION("Independent target")
    {
        targetRole = Role::Independent;
        eligible = false;
    }
    CreateSnapWindow(source, sourceRole, -19000, -19000);
    CreateSnapWindow(target, targetRole, -20000, -20000);
    Attach(source, target);
    REQUIRE(target.GetSnappingState().attachedToLeader == (eligible && targetRole == Role::Follower));
    REQUIRE(source.GetSnappingState().attachedToLeader == (eligible && sourceRole == Role::Follower));
    REQUIRE(source.GetSnappingState().followerCount == (eligible && sourceRole == Role::Leader ? 1 : 0));
    REQUIRE(target.GetSnappingState().followerCount == (eligible && targetRole == Role::Leader ? 1 : 0));
    if (eligible)
    {
        auto& leader = sourceRole == Role::Leader ? source : target;
        auto& follower = sourceRole == Role::Follower ? source : target;
        const auto beforeLeader = Rectangle(leader), beforeFollower = Rectangle(follower);
        Position(leader, beforeLeader.left + 73, beforeLeader.top + 51);
        REQUIRE(Rectangle(follower).left == beforeFollower.left + 73);
        REQUIRE(Rectangle(follower).top == beforeFollower.top + 51);
        REQUIRE(follower.GetSnappingState().attachedToLeader);
    }
}

TEST_CASE("Attachment requires both windows to enable window snapping", "[window][snapping][attachments]")
{
    SnapContext context;
    LWS::Window leader(context.platform), follower(context.platform);
    CreateSnapWindow(leader, Role::Leader, -19000, -19000);
    CreateSnapWindow(follower, Role::Follower, -20000, -20000);
    SECTION("Disabled follower")
    {
        REQUIRE(follower.SetSnappingMode(Mode::Disabled) == LWS::Result::Success);
    }
    SECTION("Screen-only follower")
    {
        REQUIRE(follower.SetSnappingMode(Mode::ScreenEdges) == LWS::Result::Success);
    }
    SECTION("Disabled leader")
    {
        REQUIRE(leader.SetSnappingMode(Mode::Disabled) == LWS::Result::Success);
        const auto target = Rectangle(follower);
        SendMessageW(Native(leader), WM_ENTERSIZEMOVE, 0, 0);
        Position(leader, target.right, target.top + 30);
        SendMessageW(Native(leader), WM_EXITSIZEMOVE, 0, 0);
    }
    const auto target = Rectangle(follower);
    auto proposed = Rectangle(leader);
    OffsetRect(&proposed, target.right + 1 - proposed.left, target.top + 30 - proposed.top);
    SendMessageW(Native(leader), WM_ENTERSIZEMOVE, 0, 0);
    const auto unchanged = Moving(leader, proposed);
    REQUIRE(EqualRect(&unchanged, &proposed));
    Position(leader, unchanged.left, unchanged.top);
    SendMessageW(Native(leader), WM_EXITSIZEMOVE, 0, 0);
    REQUIRE_FALSE(follower.GetSnappingState().attachedToLeader);
    REQUIRE(leader.GetSnappingState().followerCount == 0);
}

TEST_CASE("Occupied followers cannot transfer or create chains", "[window][snapping][attachments]")
{
    SnapContext context;
    LWS::Window leader(context.platform), follower(context.platform), another(context.platform);
    CreateSnapWindow(leader, Role::Leader, -19000, -19000);
    CreateSnapWindow(follower, Role::Follower, -20000, -20000);
    CreateSnapWindow(another, Role::Leader, -18000, -18000);
    Attach(leader, follower);
    Attach(another, follower);
    REQUIRE(leader.GetSnappingState().followerCount == 1);
    REQUIRE(another.GetSnappingState().followerCount == 0);
    REQUIRE(follower.GetSnappingState().attachedToLeader);
    REQUIRE(follower.SetSnappingRole(Role::Leader) == LWS::Result::Success);
    REQUIRE(leader.GetSnappingState().followerCount == 0);
    REQUIRE_FALSE(follower.GetSnappingState().attachedToLeader);
}

TEST_CASE("Snapping state notifications observe consistent relationships and no-op changes are silent",
          "[window][snapping][attachments]")
{
    SnapContext context;
    LWS::Window leader(context.platform), follower(context.platform);
    CreateSnapWindow(leader, Role::Leader, -19000, -19000);
    CreateSnapWindow(follower, Role::Follower, -20000, -20000);
    int leaderEvents = 0, followerEvents = 0;
    auto observe = [&](LWS::Window& window, int& count)
    {
        return window.Listen(
            [&](const LWS::AnyEvent& event)
            {
                if (std::holds_alternative<LWS::EventSnappingStateChanged>(event))
                {
                    ++count;
                    REQUIRE(follower.GetSnappingState().attachedToLeader ==
                            (leader.GetSnappingState().followerCount == 1));
                }
                return LWS::EventResponse::Unhandled;
            });
    };
    auto first = observe(leader, leaderEvents), second = observe(follower, followerEvents);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    Attach(leader, follower);
    REQUIRE(leaderEvents == 1);
    REQUIRE(followerEvents == 1);
    REQUIRE(leader.SetSnappingRole(Role::Leader) == LWS::Result::Success);
    REQUIRE(follower.SetSnappingMode(Mode::OtherWindows) == LWS::Result::Success);
    REQUIRE(leaderEvents == 1);
    REQUIRE(leader.GetSnappingState().followerCount == 1);
    REQUIRE(follower.ReleaseSnappingAttachments() == LWS::Result::Success);
    REQUIRE(leaderEvents == 2);
    REQUIRE(followerEvents == 2);
    REQUIRE(follower.ReleaseSnappingAttachments() == LWS::Result::Success);
    REQUIRE(followerEvents == 2);
    REQUIRE(follower.GetSnappingState().role == Role::Follower);
    REQUIRE(follower.GetSnappingMode() == Mode::OtherWindows);
    REQUIRE(follower.SetSnappingRole(Role::Independent) == LWS::Result::Success);
    REQUIRE(followerEvents == 3);
}

TEST_CASE("Lifecycle changes release directional attachments", "[window][snapping][attachments]")
{
    SnapContext context;
    LWS::Window leader(context.platform), follower(context.platform);
    CreateSnapWindow(leader, Role::Leader, -19000, -19000);
    CreateSnapWindow(follower, Role::Follower, -20000, -20000);
    Attach(leader, follower);
    SECTION("Resize follower")
    {
        REQUIRE(follower.RequestPlacement({.clientSize = LWS::LogicalSize{120, 100}}) == LWS::Result::Success);
    }
    SECTION("Hide follower")
    {
        REQUIRE(follower.SetVisible(false) == LWS::Result::Success);
    }
    SECTION("Destroy follower")
    {
        REQUIRE(follower.Destroy() == LWS::Result::Success);
    }
    SECTION("Role change")
    {
        REQUIRE(follower.SetSnappingRole(Role::Independent) == LWS::Result::Success);
    }
    SECTION("Mode change")
    {
        REQUIRE(leader.SetSnappingMode(Mode::ScreenEdges) == LWS::Result::Success);
    }
    SECTION("Release leader")
    {
        REQUIRE(leader.ReleaseSnappingAttachments() == LWS::Result::Success);
    }
    SECTION("Minimize leader")
    {
        REQUIRE(leader.RequestShowState(LWS::WindowShowState::Minimized) == LWS::Result::Success);
    }
    SECTION("Maximize leader")
    {
        REQUIRE(leader.RequestShowState(LWS::WindowShowState::Maximized) == LWS::Result::Success);
    }
    SECTION("Destroy leader")
    {
        REQUIRE(leader.Destroy() == LWS::Result::Success);
    }
    REQUIRE(leader.GetSnappingState().followerCount == 0);
    REQUIRE_FALSE(follower.GetSnappingState().attachedToLeader);
}

TEST_CASE("Attachment release cancels pending candidates for the current drag", "[window][snapping][attachments]")
{
    SnapContext context;
    LWS::Window leader(context.platform), follower(context.platform);
    CreateSnapWindow(leader, Role::Leader, -19000, -19000);
    CreateSnapWindow(follower, Role::Follower, -20000, -20000);
    const auto target = Rectangle(follower);
    SendMessageW(Native(leader), WM_ENTERSIZEMOVE, 0, 0);
    const auto snapped = Moving(leader, {target.right + 1, target.top + 30, target.right + 101, target.top + 130});
    Position(leader, snapped.left, snapped.top);
    REQUIRE(leader.ReleaseSnappingAttachments() == LWS::Result::Success);
    Moving(leader, snapped);
    SendMessageW(Native(leader), WM_EXITSIZEMOVE, 0, 0);
    REQUIRE_FALSE(follower.GetSnappingState().attachedToLeader);
    Attach(leader, follower);
    REQUIRE(follower.GetSnappingState().attachedToLeader);
}

TEST_CASE("Attachment notifications tolerate destruction or immediate release", "[window][snapping][attachments]")
{
    SnapContext context;
    LWS::Window leader(context.platform), follower(context.platform);
    CreateSnapWindow(leader, Role::Leader, -19000, -19000);
    CreateSnapWindow(follower, Role::Follower, -20000, -20000);
    bool destroy = false;
    SECTION("Destroy follower")
    {
        destroy = true;
    }
    SECTION("Release leader") {}
    auto listener = leader.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventSnappingStateChanged>(event) &&
                leader.GetSnappingState().followerCount != 0)
            {
                if (destroy)
                    REQUIRE(follower.Destroy() == LWS::Result::Success);
                else
                    REQUIRE(leader.ReleaseSnappingAttachments() == LWS::Result::Success);
            }
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(listener.has_value());
    Attach(leader, follower);
    REQUIRE(leader.GetSnappingState().followerCount == 0);
    REQUIRE_FALSE(follower.GetSnappingState().attachedToLeader);
}

TEST_CASE("Follower move callbacks can release or independently move a participant", "[window][snapping][attachments]")
{
    SnapContext context;
    LWS::Window leader(context.platform), follower(context.platform);
    CreateSnapWindow(leader, Role::Leader, -19000, -19000);
    CreateSnapWindow(follower, Role::Follower, -20000, -20000);
    Attach(leader, follower);
    bool reentered = false, repositionLeader = false;
    SECTION("Independent follower movement") {}
    SECTION("Independent leader movement")
    {
        repositionLeader = true;
    }
    auto listener = follower.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (!reentered && std::holds_alternative<LWS::EventMove>(event))
            {
                reentered = true;
                if (repositionLeader)
                    Position(leader, -17000, -17000);
                else
                    Position(follower, -16000, -16000);
            }
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(listener.has_value());
    const auto before = Rectangle(leader);
    Position(leader, before.left + 100, before.top + 100);
    REQUIRE(reentered);
    REQUIRE(leader.GetSnappingState().followerCount == 0);
    REQUIRE_FALSE(follower.GetSnappingState().attachedToLeader);
    if (repositionLeader)
        REQUIRE(Rectangle(leader).left == -17000);
    else
        REQUIRE(Rectangle(follower).left == -16000);
}

TEST_CASE("Native lifetimes cannot inherit pending attachments", "[window][snapping][attachments]")
{
    SnapContext context;
    LWS::Window leader(context.platform);
    CreateSnapWindow(leader, Role::Leader, -19000, -19000);
    auto target = std::make_unique<LWS::Window>(context.platform);
    CreateSnapWindow(*target, Role::Follower, -20000, -20000);
    const auto targetRect = Rectangle(*target);
    SendMessageW(Native(leader), WM_ENTERSIZEMOVE, 0, 0);
    const auto snapped = Moving(leader, {targetRect.right + 1, targetRect.top + 30, targetRect.right + 101,
                                         targetRect.top + 130});
    Position(leader, snapped.left, snapped.top);
    target.reset();
    LWS::Window replacement(context.platform);
    CreateSnapWindow(replacement, Role::Follower, -20000, -20000);
    SendMessageW(Native(leader), WM_EXITSIZEMOVE, 0, 0);
    REQUIRE(leader.GetSnappingState().followerCount == 0);
    REQUIRE_FALSE(replacement.GetSnappingState().attachedToLeader);
}

TEST_CASE("Snapping role and release APIs validate lifecycle and arguments", "[window][snapping][attachments]")
{
    SnapContext context;
    LWS::Window window(context.platform);
    REQUIRE(window.GetSnappingState() == LWS::WindowSnappingState{});
    REQUIRE(window.SetSnappingRole(Role::Leader) == LWS::Result::InvalidState);
    REQUIRE(window.ReleaseSnappingAttachments() == LWS::Result::InvalidState);
    LWS::WindowConfig config;
    config.snappingRole = static_cast<Role>(99);
    REQUIRE(window.Create(config) == LWS::Result::InvalidArgument);
    config.snappingRole = Role::Leader;
    REQUIRE(window.Create(config) == LWS::Result::Success);
    REQUIRE(window.GetSnappingState().role == Role::Leader);
    REQUIRE(window.SetSnappingRole(static_cast<Role>(99)) == LWS::Result::InvalidArgument);
    LWS::Window child(context.platform);
    REQUIRE(child.Create({.parent = &window}) == LWS::Result::Success);
    REQUIRE(child.SetSnappingRole(Role::Follower) == LWS::Result::NotSupported);
    REQUIRE(child.ReleaseSnappingAttachments() == LWS::Result::Success);
    REQUIRE(window.Destroy() == LWS::Result::Success);
    REQUIRE(window.GetSnappingState() == LWS::WindowSnappingState{Role::Leader});
    REQUIRE(window.ReleaseSnappingAttachments() == LWS::Result::InvalidState);
}

TEST_CASE("Callbacks can destroy a leader while its followers are moving", "[window][snapping][attachments]")
{
    SnapContext context;
    LWS::Window leader(context.platform), first(context.platform), second(context.platform);
    CreateSnapWindow(leader, Role::Leader, -19000, -19000);
    CreateSnapWindow(first, Role::Follower, -20000, -20000);
    CreateSnapWindow(second, Role::Follower, -18000, -18000);
    Attach(leader, first);
    Attach(leader, second, true);
    const auto stationary = Rectangle(second);
    auto listener = first.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventMove>(event))
                REQUIRE(leader.Destroy() == LWS::Result::Success);
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(listener.has_value());
    const auto before = Rectangle(leader);
    Position(leader, before.left + 100, before.top + 100);
    REQUIRE_FALSE(leader.IsCreated());
    REQUIRE_FALSE(first.GetSnappingState().attachedToLeader);
    REQUIRE_FALSE(second.GetSnappingState().attachedToLeader);
    REQUIRE(Rectangle(second).left == stationary.left);
    REQUIRE(Rectangle(second).top == stationary.top);
}

TEST_CASE("Native windows outside LWS align without attachments", "[window][snapping][attachments]")
{
    SnapContext context;
    LWS::Window leader(context.platform);
    CreateSnapWindow(leader, Role::Leader, -19000, -19000);
    struct NativeWindow
    {
        HWND handle = CreateWindowExW(0, L"STATIC", L"Snap target", WS_POPUP, -20000, -20000, 100, 100, nullptr,
                                      nullptr, GetModuleHandleW(nullptr), nullptr);
        ~NativeWindow()
        {
            if (handle)
                DestroyWindow(handle);
        }
    } target;
    REQUIRE(target.handle != nullptr);
    ShowWindow(target.handle, SW_SHOWNOACTIVATE);
    RECT rectangle{};
    REQUIRE(GetWindowRect(target.handle, &rectangle));
    SendMessageW(Native(leader), WM_ENTERSIZEMOVE, 0, 0);
    auto proposed = Rectangle(leader);
    OffsetRect(&proposed, rectangle.right + 1 - proposed.left, rectangle.top + 30 - proposed.top);
    const auto snapped = Moving(leader, proposed);
    REQUIRE(snapped.left == rectangle.right);
    Position(leader, snapped.left, snapped.top);
    SendMessageW(Native(leader), WM_EXITSIZEMOVE, 0, 0);
    REQUIRE(leader.GetSnappingState().followerCount == 0);
}

TEST_CASE("Window snapping aligns only opposite edges with positive overlap", "[window][snapping]")
{
    SnapContext context;
    LWS::Window target(context.platform), moving(context.platform);
    CreateSnapWindow(target, Role::Independent, -20000, -20000);
    CreateSnapWindow(moving, Role::Independent, -19000, -19000);
    const auto targetRect = Rectangle(target);
    RECT proposed = Rectangle(moving);
    RECT expected{};
    SECTION("Moving left to target right")
    {
        OffsetRect(&proposed, targetRect.right + 1 - proposed.left, targetRect.top + 30 - proposed.top);
        expected = proposed;
        OffsetRect(&expected, -1, 0);
    }
    SECTION("Moving right to target left")
    {
        OffsetRect(&proposed, targetRect.left - 1 - proposed.right, targetRect.top + 30 - proposed.top);
        expected = proposed;
        OffsetRect(&expected, 1, 0);
    }
    SECTION("Moving top to target bottom")
    {
        OffsetRect(&proposed, targetRect.left + 30 - proposed.left, targetRect.bottom + 1 - proposed.top);
        expected = proposed;
        OffsetRect(&expected, 0, -1);
    }
    SECTION("Moving bottom to target top")
    {
        OffsetRect(&proposed, targetRect.left + 30 - proposed.left, targetRect.top - 1 - proposed.bottom);
        expected = proposed;
        OffsetRect(&expected, 0, 1);
    }
    SECTION("Left edges do not align")
    {
        OffsetRect(&proposed, targetRect.left + 1 - proposed.left, targetRect.top + 30 - proposed.top);
        expected = proposed;
    }
    SECTION("Right edges do not align")
    {
        OffsetRect(&proposed, targetRect.right - 1 - proposed.right, targetRect.top + 30 - proposed.top);
        expected = proposed;
    }
    SECTION("Top edges do not align")
    {
        OffsetRect(&proposed, targetRect.left + 30 - proposed.left, targetRect.top + 1 - proposed.top);
        expected = proposed;
    }
    SECTION("Bottom edges do not align")
    {
        OffsetRect(&proposed, targetRect.left + 30 - proposed.left, targetRect.bottom - 1 - proposed.bottom);
        expected = proposed;
    }
    SECTION("Corner-only contact does not snap horizontally")
    {
        OffsetRect(&proposed, targetRect.right + 1 - proposed.left, targetRect.bottom - proposed.top);
        expected = proposed;
    }
    SECTION("Corner-only contact does not snap vertically")
    {
        OffsetRect(&proposed, targetRect.right - proposed.left, targetRect.bottom + 1 - proposed.top);
        expected = proposed;
    }
    const auto snapped = Moving(moving, proposed);
    REQUIRE(EqualRect(&snapped, &expected));
}

TEST_CASE("A follower can detach and attach to another leader in one drag", "[window][snapping][attachments]")
{
    SnapContext context;
    LWS::Window former(context.platform), next(context.platform), follower(context.platform), sibling(context.platform);
    CreateSnapWindow(former, Role::Leader, -20000, -20000);
    CreateSnapWindow(next, Role::Leader, -18000, -18000);
    CreateSnapWindow(follower, Role::Follower, -19000, -19000);
    CreateSnapWindow(sibling, Role::Follower, -17000, -17000);
    Attach(follower, former);
    Attach(sibling, former, true);
    REQUIRE(former.GetSnappingState().followerCount == 2);
    const auto oldLeader = Rectangle(former), oldSibling = Rectangle(sibling), target = Rectangle(next);
    SendMessageW(Native(follower), WM_ENTERSIZEMOVE, 0, 0);
    auto proposed = Rectangle(follower);
    OffsetRect(&proposed, target.right + 1 - proposed.left, target.top + 30 - proposed.top);
    const auto snapped = Moving(follower, proposed);
    REQUIRE(snapped.left == target.right);
    REQUIRE_FALSE(follower.GetSnappingState().attachedToLeader);
    REQUIRE(former.GetSnappingState().followerCount == 1);
    const auto currentLeader = Rectangle(former), currentSibling = Rectangle(sibling);
    REQUIRE(EqualRect(&oldLeader, &currentLeader));
    REQUIRE(EqualRect(&oldSibling, &currentSibling));
    Position(follower, snapped.left, snapped.top);
    bool shouldAttach = true;
    SECTION("Drop attaches to the new leader") {}
    SECTION("Explicit release still cancels attachment")
    {
        shouldAttach = false;
        REQUIRE(follower.ReleaseSnappingAttachments() == LWS::Result::Success);
    }
    SendMessageW(Native(follower), WM_EXITSIZEMOVE, 0, 0);
    const bool attached = follower.GetSnappingState().attachedToLeader;
    REQUIRE(attached == shouldAttach);
    REQUIRE(next.GetSnappingState().followerCount == (attached ? 1 : 0));
    const auto before = Rectangle(follower);
    Position(former, oldLeader.left + 20, oldLeader.top + 10);
    REQUIRE(Rectangle(follower).left == before.left);
    REQUIRE(Rectangle(sibling).left == oldSibling.left + 20);
    Position(next, target.left + 73, target.top + 51);
    REQUIRE(Rectangle(follower).left == before.left + (attached ? 73 : 0));
    REQUIRE(Rectangle(follower).top == before.top + (attached ? 51 : 0));
}

TEST_CASE("A follower touching two leaders attaches to the horizontal candidate", "[window][snapping][attachments]")
{
    SnapContext context;
    LWS::Window horizontal(context.platform), vertical(context.platform), follower(context.platform);
    CreateSnapWindow(horizontal, Role::Leader, -20000, -20000);
    CreateSnapWindow(vertical, Role::Leader, -18000, -18000);
    CreateSnapWindow(follower, Role::Follower, -17000, -17000);
    const auto left = Rectangle(horizontal);
    Position(vertical, left.right + 30, left.top - 30);
    const auto above = Rectangle(vertical);
    bool cancel = false;
    SECTION("Horizontal candidate wins") {}
    SECTION("Release during notification cancels remaining candidates")
    {
        cancel = true;
    }
    auto listener = follower.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (cancel && std::holds_alternative<LWS::EventSnappingStateChanged>(event) &&
                follower.GetSnappingState().attachedToLeader)
                REQUIRE(horizontal.ReleaseSnappingAttachments() == LWS::Result::Success);
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(listener.has_value());
    SendMessageW(Native(follower), WM_ENTERSIZEMOVE, 0, 0);
    auto proposed = Rectangle(follower);
    OffsetRect(&proposed, left.right + 1 - proposed.left, above.bottom + 1 - proposed.top);
    const auto snapped = Moving(follower, proposed);
    REQUIRE(snapped.left == left.right);
    REQUIRE(snapped.top == above.bottom);
    Position(follower, snapped.left, snapped.top);
    SendMessageW(Native(follower), WM_EXITSIZEMOVE, 0, 0);
    REQUIRE(horizontal.GetSnappingState().followerCount == (cancel ? 0 : 1));
    REQUIRE(vertical.GetSnappingState().followerCount == 0);
    REQUIRE(follower.GetSnappingState().attachedToLeader == !cancel);
}

TEST_CASE("All LWS roles opt out of attracting windows when window snapping is disabled", "[window][snapping]")
{
    SnapContext context;
    LWS::Window moving(context.platform), target(context.platform);
    Role role = Role::Leader;
    SECTION("Leader target") {}
    SECTION("Follower target")
    {
        role = Role::Follower;
    }
    SECTION("Independent target")
    {
        role = Role::Independent;
    }
    CreateSnapWindow(moving, Role::Leader, -19000, -19000);
    CreateSnapWindow(target, role, -20000, -20000);
    const auto rectangle = Rectangle(target);
    auto proposed = Rectangle(moving);
    OffsetRect(&proposed, rectangle.right + 1 - proposed.left, rectangle.top + 30 - proposed.top);
    SendMessageW(Native(moving), WM_ENTERSIZEMOVE, 0, 0);
    for (const auto mode :
         {Mode::OtherWindows, Mode::Disabled, Mode::ScreenEdges, Mode::ScreenEdgesAndOtherWindows, Mode::Disabled})
    {
        REQUIRE(target.SetSnappingMode(mode) == LWS::Result::Success);
        const auto result = Moving(moving, proposed);
        const bool enabled = mode == Mode::OtherWindows || mode == Mode::ScreenEdgesAndOtherWindows;
        REQUIRE(result.left == (enabled ? rectangle.right : proposed.left));
        REQUIRE(result.top == proposed.top);
    }
    SendMessageW(Native(moving), WM_EXITSIZEMOVE, 0, 0);
    REQUIRE(moving.GetSnappingState().followerCount == 0);
}

TEST_CASE("Slow cursor movement releases screen snapping despite rebased native rectangles", "[window][snapping]")
{
    SnapContext context;
    LWS::Window window(context.platform);
    REQUIRE(window.Create({.clientSize = {100, 100}, .snappingMode = Mode::ScreenEdges}) == LWS::Result::Success);
    MONITORINFO monitor{sizeof(monitor)};
    REQUIRE(GetMonitorInfoW(MonitorFromWindow(Native(window), MONITOR_DEFAULTTONEAREST), &monitor));
    const auto work = monitor.rcWork;
    const auto size = Rectangle(window);
    const LONG width = size.right - size.left, height = size.bottom - size.top;
    POINT direction{1, 0};
    RECT raw{work.left + 1, (work.top + work.bottom) / 2, work.left + 1 + width, (work.top + work.bottom) / 2 + height};
    SECTION("Left edge") {}
    SECTION("Right edge")
    {
        OffsetRect(&raw, work.right - 1 - raw.right, 0);
        direction = {-1, 0};
    }
    SECTION("Top edge")
    {
        OffsetRect(&raw, (work.left + work.right) / 2 - raw.left, work.top + 1 - raw.top);
        direction = {0, 1};
    }
    SECTION("Bottom edge")
    {
        OffsetRect(&raw, (work.left + work.right) / 2 - raw.left, work.bottom - 1 - raw.bottom);
        direction = {0, -1};
    }
    SECTION("Corner releases both axes")
    {
        OffsetRect(&raw, 0, work.top + 1 - raw.top);
        direction = {1, 1};
    }
    LWS::internal::WindowDragAnchor anchor;
    POINT cursor{400, 300};
    const RECT initial = raw;
    anchor.Apply(raw, cursor);
    auto applied = Moving(window, raw);
    REQUIRE(!EqualRect(&applied, &initial));
    // Model native proposals rebased onto the last magnetically corrected placement.
    for (int step = 1; step <= 100; ++step)
    {
        cursor.x += direction.x;
        cursor.y += direction.y;
        raw = applied;
        OffsetRect(&raw, direction.x, direction.y);
        anchor.Apply(raw, cursor);
        RECT expected = initial;
        OffsetRect(&expected, direction.x * step, direction.y * step);
        REQUIRE(EqualRect(&raw, &expected));
        applied = Moving(window, raw);
        REQUIRE(applied.right - applied.left == width);
        REQUIRE(applied.bottom - applied.top == height);
    }
    REQUIRE(EqualRect(&applied, &raw));
    anchor.Reset();
    REQUIRE_FALSE(anchor.Active());
    const RECT newStart{work.left + 1, work.top + 1, work.left + width + 1, work.top + height + 1};
    raw = newStart;
    anchor.Apply(raw, {1200, 800});
    REQUIRE(EqualRect(&raw, &newStart));
}

TEST_CASE("Drag cursor anchors rebase on native size changes", "[window][snapping]")
{
    LWS::internal::WindowDragAnchor anchor;
    RECT rectangle{-200, -100, 0, 100};
    anchor.Apply(rectangle, {-150, -80});
    rectangle = {-200, -100, 0, 100};
    anchor.Apply(rectangle, {-140, -75});
    REQUIRE(rectangle.left == -190);
    REQUIRE(rectangle.top == -95);
    RECT resized{-180, -90, 120, 210};
    const auto expected = resized;
    anchor.Apply(resized, {-130, -70});
    REQUIRE(EqualRect(&resized, &expected));
    anchor.Apply(resized, {-125, -65});
    REQUIRE(resized.left == expected.left + 5);
    REQUIRE(resized.top == expected.top + 5);
    REQUIRE(resized.right - resized.left == 300);
}

TEST_CASE("A slowly dragged follower can pull free and snap back to the same leader", "[window][snapping][attachments]")
{
    SnapContext context;
    LWS::Window leader(context.platform), follower(context.platform);
    CreateSnapWindow(leader, Role::Leader, -20000, -20000);
    CreateSnapWindow(follower, Role::Follower, -19000, -19000);
    Attach(follower, leader);
    const auto stationary = Rectangle(leader), start = Rectangle(follower);
    int changes = 0;
    auto listener = follower.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventSnappingStateChanged>(event))
                ++changes;
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(listener.has_value());
    SendMessageW(Native(follower), WM_ENTERSIZEMOVE, 0, 0);
    LWS::internal::WindowDragAnchor anchor;
    POINT cursor{400, 300};
    RECT raw = start;
    OffsetRect(&raw, 1, 0);
    anchor.Apply(raw, cursor);
    auto applied = Moving(follower, raw);
    Position(follower, applied.left, applied.top);
    REQUIRE_FALSE(follower.GetSnappingState().attachedToLeader);
    REQUIRE(leader.GetSnappingState().followerCount == 0);
    for (int step = 1; step <= 100; ++step)
    {
        ++cursor.x;
        raw = applied;
        OffsetRect(&raw, 1, 0);
        anchor.Apply(raw, cursor);
        applied = Moving(follower, raw);
        Position(follower, applied.left, applied.top);
    }
    REQUIRE(applied.left == start.left + 101);
    const auto still = Rectangle(leader);
    REQUIRE(EqualRect(&stationary, &still));
    cursor.x = 400;
    raw = applied;
    anchor.Apply(raw, cursor);
    applied = Moving(follower, raw);
    REQUIRE(applied.left == start.left);
    Position(follower, applied.left, applied.top);
    REQUIRE_FALSE(follower.GetSnappingState().attachedToLeader);
    SendMessageW(Native(follower), WM_EXITSIZEMOVE, 0, 0);
    REQUIRE(follower.GetSnappingState().attachedToLeader);
    REQUIRE(leader.GetSnappingState().followerCount == 1);
    REQUIRE(changes == 2);
    const auto before = Rectangle(follower);
    Position(leader, stationary.left + 75, stationary.top + 50);
    REQUIRE(Rectangle(follower).left == before.left + 75);
    REQUIRE(Rectangle(follower).top == before.top + 50);
}

#endif
