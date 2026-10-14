#include "scene_mapping.h"

#include "signature_scan.h"

namespace soma::mapping {
namespace {

constexpr std::string_view kSceneEntry =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 "
    "48 83 EC 50 FF 05 ?? ?? ?? ?? 33 C0 48 8B F1 "
    "89 05 ?? ?? ?? ?? 89 05 ?? ?? ?? ?? 89 05 ?? ?? ?? ?? "
    "0F 29 74 24 40 0F 28 F1";

constexpr std::string_view kViewportDispatch =
    "48 8B 53 10 80 7A 29 00 74 ?? 0F 28 D6 44 8B CD "
    "48 8B CE E8 ?? ?? ?? ?? 48 8B 1B 48 3B DF 75 ??";

constexpr std::string_view kViewportEntry =
    "48 89 5C 24 10 44 89 4C 24 20 55 56 57 41 54 41 55 41 56 41 57 "
    "48 83 EC 50 48 8B 42 38 4C 8B 72 30 48 8B FA 0F 29 74 24 40 "
    "0F 28 F2 48 89 84 24 90 00 00 00 48 8B 41 38 4C 8B F9 "
    "4C 8B 60 38 BA 21 00 00 00 41 8B D9 49 8B 04 24 49 8B CC "
    "FF 50 50";

constexpr std::string_view kCameraLoad =
    "4C 8B 6F ?? 85 C0 0F 95 C2 4D 85 ED 74 ?? "
    "49 8B CD E8 ?? ?? ?? ??";

} // namespace

SceneRenderEvidence ValidateSceneRender(
    const std::span<const std::uint8_t> code) noexcept {
    SceneRenderEvidence result{};
    if (code.size() < kSceneRenderInspectionBytes) return result;

    const auto entryPattern = signature::Parse(kSceneEntry);
    const auto dispatchPattern = signature::Parse(kViewportDispatch);
    if (entryPattern.status != signature::ParseStatus::valid ||
        dispatchPattern.status != signature::ParseStatus::valid) {
        result.status = SceneValidationStatus::malformedPattern;
        return result;
    }

    const auto window = code.first(kSceneRenderInspectionBytes);
    const auto entry = signature::FindUnique(window, entryPattern.pattern);
    if (entry.status != signature::MatchStatus::unique || entry.offset != 0) {
        result.status = SceneValidationStatus::entryMismatch;
        return result;
    }
    const auto dispatch = signature::FindUnique(window, dispatchPattern.pattern);
    if (dispatch.status != signature::MatchStatus::unique) {
        result.status = dispatch.status == signature::MatchStatus::ambiguous
            ? SceneValidationStatus::viewportDispatchAmbiguous
            : SceneValidationStatus::viewportDispatchMissing;
        return result;
    }

    constexpr std::size_t kCallOffset = 19;
    result.status = SceneValidationStatus::valid;
    result.viewportRenderCallOffset = dispatch.offset + kCallOffset;
    return result;
}

ViewportRenderEvidence ValidateViewportRender(
    const std::span<const std::uint8_t> code) noexcept {
    ViewportRenderEvidence result{};
    if (code.size() < kViewportRenderInspectionBytes) return result;

    const auto entryPattern = signature::Parse(kViewportEntry);
    const auto cameraPattern = signature::Parse(kCameraLoad);
    if (entryPattern.status != signature::ParseStatus::valid ||
        cameraPattern.status != signature::ParseStatus::valid) {
        result.status = ViewportValidationStatus::malformedPattern;
        return result;
    }

    const auto window = code.first(kViewportRenderInspectionBytes);
    const auto entry = signature::FindUnique(window, entryPattern.pattern);
    if (entry.status != signature::MatchStatus::unique || entry.offset != 0) {
        result.status = ViewportValidationStatus::entryMismatch;
        return result;
    }
    const auto camera = signature::FindUnique(window, cameraPattern.pattern);
    if (camera.status != signature::MatchStatus::unique) {
        result.status = camera.status == signature::MatchStatus::ambiguous
            ? ViewportValidationStatus::cameraLoadAmbiguous
            : ViewportValidationStatus::cameraLoadMissing;
        return result;
    }
    constexpr std::size_t kCameraDisplacementIndex = 3;
    const std::uint8_t offset = window[camera.offset + kCameraDisplacementIndex];
    if (offset < sizeof(void*) || offset > 0x80 ||
        (offset % alignof(void*)) != 0) {
        result.status = ViewportValidationStatus::invalidCameraOffset;
        return result;
    }

    result.status = ViewportValidationStatus::valid;
    result.cameraOffset = offset;
    return result;
}

std::string_view Describe(const SceneValidationStatus status) noexcept {
    switch (status) {
    case SceneValidationStatus::valid: return "valid";
    case SceneValidationStatus::windowTooSmall: return "inspection window is too small";
    case SceneValidationStatus::malformedPattern: return "internal pattern is malformed";
    case SceneValidationStatus::entryMismatch: return "scene render entry does not match";
    case SceneValidationStatus::viewportDispatchMissing: return "viewport dispatch is missing";
    case SceneValidationStatus::viewportDispatchAmbiguous: return "viewport dispatch is ambiguous";
    }
    return "unknown scene validation status";
}

std::string_view Describe(const ViewportValidationStatus status) noexcept {
    switch (status) {
    case ViewportValidationStatus::valid: return "valid";
    case ViewportValidationStatus::windowTooSmall: return "inspection window is too small";
    case ViewportValidationStatus::malformedPattern: return "internal pattern is malformed";
    case ViewportValidationStatus::entryMismatch: return "viewport render entry does not match";
    case ViewportValidationStatus::cameraLoadMissing: return "viewport camera load is missing";
    case ViewportValidationStatus::cameraLoadAmbiguous: return "viewport camera load is ambiguous";
    case ViewportValidationStatus::invalidCameraOffset: return "viewport camera offset is invalid";
    }
    return "unknown viewport validation status";
}

} // namespace soma::mapping
