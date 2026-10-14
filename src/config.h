#pragma once

#include <string>

namespace soma::config {

struct Features {
    bool renderUnlock = false;
    bool presentationLimiter = false;
    bool cameraInterpolation = false;
    bool handsRootInterpolation = false;
    bool handsPoseInterpolation = false;
    bool toolInterpolation = false;
    bool worldRootInterpolation = false;
    bool worldPoseInterpolation = false;
    bool agentRootInterpolation = false;
    bool agentPoseInterpolation = false;
    bool ragdollPoseInterpolation = false;
    bool particleInterpolation = false;
    bool imageTrailTiming = false;
    bool mouseOverlay = false;
    bool guiMouseOverlay = false;
};

struct Settings {
    bool enabled = true;
    bool diagnostics = true;
    // [Logging]. None of these affect what the fix does; the timing the
    // smoothing features rely on is installed whenever one of them is on.
    bool healthReports = true;
    unsigned int reportIntervalSeconds = 5;
    bool detailedReports = false;
    Features features{};
    unsigned int targetFps = 0;
};

struct LoadResult {
    bool ok = false;
    Settings settings{};
    std::wstring error;
};

[[nodiscard]] LoadResult Load(const std::wstring& path);
[[nodiscard]] bool AnyBehavioralFeatureEnabled(const Features& features) noexcept;
// True when a feature needs the fixed-update and presentation timing hooks,
// which is every feature except the bare render unlock.
[[nodiscard]] bool AnySmoothingFeatureEnabled(const Features& features) noexcept;
[[nodiscard]] bool AnyRuntimeWorkEnabled(const Settings& settings) noexcept;

} // namespace soma::config

