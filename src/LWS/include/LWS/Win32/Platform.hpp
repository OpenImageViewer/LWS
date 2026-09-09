#pragma once

#include <LWS/Result.hpp>

namespace LWS::Win32
{
    enum class DpiPolicy
    {
        /// Uses per-monitor-v2 awareness when available and system awareness on legacy Windows.
        ConfigurePerMonitorV2,
        /// Requires the process to already use per-monitor-v2 awareness.
        AdoptExistingPerMonitorV2
    };

    struct ProcessConfig
    {
        DpiPolicy dpiPolicy{DpiPolicy::ConfigurePerMonitorV2};
    };

    [[nodiscard]] Result BootstrapProcess(const ProcessConfig& config = {});
}  // namespace LWS::Win32
