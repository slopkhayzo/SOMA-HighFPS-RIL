#include "engine_mapping.h"

#include "signature_scan.h"

#include <algorithm>
#include <cstring>

namespace soma::mapping {
namespace {

constexpr std::string_view kEngineRunEntry =
    "40 56 57 41 54 41 56 41 57 48 81 EC E0 00 00 00 "
    "48 8B F1 48 8D 0D ?? ?? ?? ?? 0F 29 B4 24 C0 00 00 00 "
    "44 0F 29 5C 24 70 E8 ?? ?? ?? ?? 48 8B 4E 40";

constexpr std::string_view kRenderGate =
    "80 3E 00 74 ?? 40 84 FF 0F 85 ?? ?? ?? ?? "
    "80 7E ?? 00 74 ?? 45 84 F6";

constexpr std::string_view kFixedUpdateDispatch =
    "48 8B 4E 40 BA 04 00 00 00 0F 28 D0 E8 ?? ?? ?? ?? "
    "48 8B 4E ?? E8 ?? ?? ?? ?? "
    "48 8B 4E 40 BA 05 00 00 00 0F 28 D0 E8 ?? ?? ?? ??";

constexpr std::string_view kFlushCall =
    "4C 8B 9E C0 00 00 00 49 8B 4B 38 48 8B 01 "
    "FF 90 B8 01 00 00";

constexpr std::string_view kSchedulerClock =
    "E8 ?? ?? ?? ?? 48 8B F8 E8 ?? ?? ?? ?? "
    "48 8B 4E 70 48 8B 11 8B D8 FF 52 10";

constexpr std::string_view kSceneRender =
    "44 8B 46 7C 48 8B 8E C8 00 00 00 F3 0F 10 4E 04 "
    "E8 ?? ?? ?? ??";

constexpr std::string_view kPresentCall =
    "48 8B 86 C0 00 00 00 48 8B 48 38 48 8B 01 "
    "FF 90 B0 01 00 00";

ValidationStatus TranslateMissing(const signature::MatchResult match,
                                  const ValidationStatus missing,
                                  const ValidationStatus ambiguous) noexcept {
    return match.status == signature::MatchStatus::ambiguous ? ambiguous : missing;
}

} // namespace

std::string_view EngineRunEntryPattern() noexcept {
    return kEngineRunEntry;
}

EngineRunEvidence ValidateEngineRun(
    const std::span<const std::uint8_t> code) noexcept {
    EngineRunEvidence result{};
    if (code.size() < kEngineRunInspectionBytes) {
        result.status = ValidationStatus::windowTooSmall;
        return result;
    }

    const auto updatePattern = signature::Parse(kFixedUpdateDispatch);
    const auto gatePattern = signature::Parse(kRenderGate);
    const auto clockPattern = signature::Parse(kSchedulerClock);
    const auto scenePattern = signature::Parse(kSceneRender);
    const auto flushPattern = signature::Parse(kFlushCall);
    const auto presentPattern = signature::Parse(kPresentCall);
    if (updatePattern.status != signature::ParseStatus::valid ||
        gatePattern.status != signature::ParseStatus::valid ||
        clockPattern.status != signature::ParseStatus::valid ||
        scenePattern.status != signature::ParseStatus::valid ||
        flushPattern.status != signature::ParseStatus::valid ||
        presentPattern.status != signature::ParseStatus::valid) {
        result.status = ValidationStatus::malformedEvidencePattern;
        return result;
    }

    const auto window = code.first(kEngineRunInspectionBytes);
    const auto update = signature::FindUnique(window, updatePattern.pattern);
    if (update.status != signature::MatchStatus::unique) {
        result.status = TranslateMissing(
            update, ValidationStatus::updateDispatchMissing,
            ValidationStatus::updateDispatchAmbiguous);
        return result;
    }
    constexpr std::size_t kFirstDispatchCall = 12;
    constexpr std::size_t kSecondDispatchCall = 38;
    std::int32_t firstDisplacement = 0;
    std::int32_t secondDisplacement = 0;
    std::memcpy(&firstDisplacement,
                window.data() + update.offset + kFirstDispatchCall + 1,
                sizeof(firstDisplacement));
    std::memcpy(&secondDisplacement,
                window.data() + update.offset + kSecondDispatchCall + 1,
                sizeof(secondDisplacement));
    const std::int64_t firstTarget =
        static_cast<std::int64_t>(update.offset + kFirstDispatchCall + 5) +
        firstDisplacement;
    const std::int64_t secondTarget =
        static_cast<std::int64_t>(update.offset + kSecondDispatchCall + 5) +
        secondDisplacement;
    if (firstTarget != secondTarget) {
        result.status = ValidationStatus::updateDispatchTargetMismatch;
        return result;
    }
    constexpr std::size_t kEngineTimerDisplacementIndex = 20;
    const std::uint8_t engineTimerOffset =
        window[update.offset + kEngineTimerDisplacementIndex];
    if (engineTimerOffset < 0x20 || engineTimerOffset > 0xc0 ||
        (engineTimerOffset % alignof(void*)) != 0) {
        result.status = ValidationStatus::invalidEngineTimerOffset;
        return result;
    }

    const auto gate = signature::FindUnique(window, gatePattern.pattern);
    if (gate.status != signature::MatchStatus::unique) {
        result.status = TranslateMissing(gate, ValidationStatus::renderGateMissing,
                                         ValidationStatus::renderGateAmbiguous);
        return result;
    }
    constexpr std::size_t kLimitFpsDisplacementIndex = 16;
    const std::uint8_t limitFpsOffset =
        window[gate.offset + kLimitFpsDisplacementIndex];
    if (limitFpsOffset < 0x40 || limitFpsOffset > 0xc0 ||
        (limitFpsOffset % alignof(void*)) != 0) {
        result.status = ValidationStatus::invalidLimitFpsOffset;
        return result;
    }

    const auto schedulerClock =
        signature::FindUnique(window, clockPattern.pattern);
    if (schedulerClock.status != signature::MatchStatus::unique) {
        result.status = TranslateMissing(
            schedulerClock, ValidationStatus::schedulerClockMissing,
            ValidationStatus::schedulerClockAmbiguous);
        return result;
    }

    const auto sceneRender = signature::FindUnique(window, scenePattern.pattern);
    if (sceneRender.status != signature::MatchStatus::unique) {
        result.status = TranslateMissing(
            sceneRender, ValidationStatus::sceneRenderMissing,
            ValidationStatus::sceneRenderAmbiguous);
        return result;
    }

    const auto flush = signature::FindUnique(window, flushPattern.pattern);
    if (flush.status != signature::MatchStatus::unique) {
        result.status = TranslateMissing(flush, ValidationStatus::flushCallMissing,
                                         ValidationStatus::flushCallAmbiguous);
        return result;
    }
    const auto present = signature::FindUnique(window, presentPattern.pattern);
    if (present.status != signature::MatchStatus::unique) {
        result.status = TranslateMissing(present,
                                         ValidationStatus::presentCallMissing,
                                         ValidationStatus::presentCallAmbiguous);
        return result;
    }
    if (!(update.offset < gate.offset && gate.offset < schedulerClock.offset &&
          schedulerClock.offset < sceneRender.offset &&
          sceneRender.offset < flush.offset && flush.offset < present.offset)) {
        result.status = ValidationStatus::invalidOrdering;
        return result;
    }

    result.status = ValidationStatus::valid;
    result.updateDispatchCallOffset = update.offset + kFirstDispatchCall;
    result.schedulerClockCallOffset = schedulerClock.offset;
    constexpr std::size_t kSceneRenderCall = 16;
    result.sceneRenderCallOffset = sceneRender.offset + kSceneRenderCall;
    result.renderGateOffset = gate.offset;
    result.flushCallOffset = flush.offset;
    result.presentCallOffset = present.offset;
    result.limitFpsOffset = limitFpsOffset;
    result.engineTimerOffset = engineTimerOffset;
    return result;
}

std::string_view Describe(const ValidationStatus status) noexcept {
    switch (status) {
    case ValidationStatus::valid: return "valid";
    case ValidationStatus::windowTooSmall: return "inspection window is too small";
    case ValidationStatus::malformedEvidencePattern: return "internal evidence pattern is malformed";
    case ValidationStatus::updateDispatchMissing: return "fixed-update dispatcher is missing";
    case ValidationStatus::updateDispatchAmbiguous: return "fixed-update dispatcher is ambiguous";
    case ValidationStatus::updateDispatchTargetMismatch: return "fixed-update calls have different targets";
    case ValidationStatus::invalidEngineTimerOffset: return "engine logic-timer displacement is invalid";
    case ValidationStatus::renderGateMissing: return "render gate is missing";
    case ValidationStatus::renderGateAmbiguous: return "render gate is ambiguous";
    case ValidationStatus::invalidLimitFpsOffset: return "LimitFPS displacement is invalid";
    case ValidationStatus::sceneRenderMissing: return "scene render call is missing";
    case ValidationStatus::sceneRenderAmbiguous: return "scene render call is ambiguous";
    case ValidationStatus::schedulerClockMissing: return "scheduler clock call is missing";
    case ValidationStatus::schedulerClockAmbiguous: return "scheduler clock call is ambiguous";
    case ValidationStatus::flushCallMissing: return "flush virtual call is missing";
    case ValidationStatus::flushCallAmbiguous: return "flush virtual call is ambiguous";
    case ValidationStatus::presentCallMissing: return "present virtual call is missing";
    case ValidationStatus::presentCallAmbiguous: return "present virtual call is ambiguous";
    case ValidationStatus::invalidOrdering: return "render boundary ordering is invalid";
    }
    return "unknown validation status";
}

} // namespace soma::mapping
