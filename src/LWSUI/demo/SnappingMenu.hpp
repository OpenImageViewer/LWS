#pragma once
#include <LWSUI/Menu.hpp>
#include <LWS/Window.hpp>
#include <array>
#include <utility>

namespace LWSUI::demo
{
    inline MenuItem SnappingMenu(LWS::Window& window, std::function<void()> changed)
    {
        using Mode = LWS::WindowSnappingMode;
        const bool supported = window.GetPlatformContext().GetBackendId() == LWS::BackendId::Win32 &&
                               window.GetParent() == nullptr && !window.IsPopup();
        MenuItem menu{.label = "&Snapping", .enabled = supported};
        constexpr std::array modes{Mode::Disabled, Mode::ScreenEdges, Mode::OtherWindows,
                                   Mode::ScreenEdgesAndOtherWindows};
        constexpr std::array labels{"&Disabled", "&Screen edges", "&Other windows", "Screen edges &and other windows"};
        for (size_t index = 0; index < modes.size(); ++index)
        {
            const auto mode = modes[index];
            menu.submenu.push_back({.label = labels[index],
                                    .action =
                                        [&window, mode, changed]
                                    {
                                        if (window.SetSnappingMode(mode) == LWS::Result::Success && changed)
                                            changed();
                                    },
                                    .enabled = supported,
                                    .checked = window.GetSnappingMode() == mode});
        }
        const auto state = window.GetSnappingState();
        using Role = LWS::WindowSnappingRole;
        MenuItem roles{.label = "Attachment &role"};
        constexpr std::array roleValues{Role::Independent, Role::Leader, Role::Follower};
        constexpr std::array roleLabels{"&Independent", "&Leader", "&Follower"};
        for (size_t index = 0; index < roleValues.size(); ++index)
        {
            const auto role = roleValues[index];
            roles.submenu.push_back({.label = roleLabels[index],
                                     .action =
                                         [&window, role, changed]
                                     {
                                         if (window.SetSnappingRole(role) == LWS::Result::Success && changed)
                                             changed();
                                     },
                                     .checked = state.role == role});
        }
        menu.submenu.push_back(MenuItem::Separator());
        menu.submenu.push_back(std::move(roles));
        const auto status = state.role == Role::Leader ? "Followers: " + std::to_string(state.followerCount)
                            : state.attachedToLeader   ? std::string("Attached to a leader")
                                                       : std::string("Unattached");
        menu.submenu.push_back({.label = status, .enabled = false});
        menu.submenu.push_back({.label = state.role == Role::Leader ? "&Release followers" : "&Detach",
                                .action =
                                    [&window, changed]
                                {
                                    if (window.ReleaseSnappingAttachments() == LWS::Result::Success && changed)
                                        changed();
                                },
                                .enabled = state.attachedToLeader || state.followerCount != 0});
        return menu;
    }
}  // namespace LWSUI::demo
