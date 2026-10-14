#pragma once

#include "target_mapping.h"

#include <string>

namespace soma::runtime {

struct Options {
    bool renderUnlock = false;
    bool telemetry = false;
    bool cameraInterpolation = false;
    bool handsRootInterpolation = false;
    bool handsPoseInterpolation = false;
    bool worldRootInterpolation = false;
    bool worldPoseInterpolation = false;
    bool agentRootInterpolation = false;
    bool agentPoseInterpolation = false;
    bool guiMouseOverlay = false;
    bool mouseLookPreview = false;
    bool ragdollPoseInterpolation = false;
    unsigned int reportIntervalSeconds = 5;
    // Periodic reports are a one-line health summary unless this is set; the
    // full counter dump is always written once at shutdown.
    bool detailedReports = false;
    // false writes no periodic line at all; the shutdown report remains.
    bool healthReports = true;
};

[[nodiscard]] bool Initialize(const target::RuntimeMappings& mappings,
                              const Options& options,
                              std::wstring& error) noexcept;

[[nodiscard]] bool InitializeRenderUnlock(
    const target::RuntimeMappings& mappings, std::wstring& error) noexcept;

} // namespace soma::runtime
