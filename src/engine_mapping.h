#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace soma::mapping {

inline constexpr std::size_t kEngineRunInspectionBytes = 0x900;

enum class ValidationStatus {
    valid,
    windowTooSmall,
    malformedEvidencePattern,
    updateDispatchMissing,
    updateDispatchAmbiguous,
    updateDispatchTargetMismatch,
    invalidEngineTimerOffset,
    renderGateMissing,
    renderGateAmbiguous,
    invalidLimitFpsOffset,
    sceneRenderMissing,
    sceneRenderAmbiguous,
    schedulerClockMissing,
    schedulerClockAmbiguous,
    flushCallMissing,
    flushCallAmbiguous,
    presentCallMissing,
    presentCallAmbiguous,
    invalidOrdering,
};

struct EngineRunEvidence {
    ValidationStatus status = ValidationStatus::windowTooSmall;
    std::size_t updateDispatchCallOffset = 0;
    std::size_t schedulerClockCallOffset = 0;
    std::size_t renderGateOffset = 0;
    std::size_t sceneRenderCallOffset = 0;
    std::size_t flushCallOffset = 0;
    std::size_t presentCallOffset = 0;
    std::uint8_t limitFpsOffset = 0;
    std::uint8_t engineTimerOffset = 0;
};

[[nodiscard]] std::string_view EngineRunEntryPattern() noexcept;
[[nodiscard]] EngineRunEvidence ValidateEngineRun(
    std::span<const std::uint8_t> code) noexcept;
[[nodiscard]] std::string_view Describe(ValidationStatus status) noexcept;

} // namespace soma::mapping
