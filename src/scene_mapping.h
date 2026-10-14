#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace soma::mapping {

inline constexpr std::size_t kSceneRenderInspectionBytes = 0xf2;
inline constexpr std::size_t kViewportRenderInspectionBytes = 0x80;

enum class SceneValidationStatus {
    valid,
    windowTooSmall,
    malformedPattern,
    entryMismatch,
    viewportDispatchMissing,
    viewportDispatchAmbiguous,
};

struct SceneRenderEvidence {
    SceneValidationStatus status = SceneValidationStatus::windowTooSmall;
    std::size_t viewportRenderCallOffset = 0;
};

enum class ViewportValidationStatus {
    valid,
    windowTooSmall,
    malformedPattern,
    entryMismatch,
    cameraLoadMissing,
    cameraLoadAmbiguous,
    invalidCameraOffset,
};

struct ViewportRenderEvidence {
    ViewportValidationStatus status = ViewportValidationStatus::windowTooSmall;
    std::uint8_t cameraOffset = 0;
};

[[nodiscard]] SceneRenderEvidence ValidateSceneRender(
    std::span<const std::uint8_t> code) noexcept;
[[nodiscard]] ViewportRenderEvidence ValidateViewportRender(
    std::span<const std::uint8_t> code) noexcept;
[[nodiscard]] std::string_view Describe(SceneValidationStatus status) noexcept;
[[nodiscard]] std::string_view Describe(ViewportValidationStatus status) noexcept;

} // namespace soma::mapping
