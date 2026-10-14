#include "world_mapping.h"

#include "signature_scan.h"

#include <cstdint>
#include <limits>

namespace soma::mapping {
namespace {

// SOMA GOG 1.102: cLuxProp::OnUpdate(float), RVA 0x17A100.  In addition to
// the prologue, retain the active-state access and the first three direct
// update calls so a nearby float-taking prop method cannot satisfy the gate.
constexpr std::string_view kLuxPropOnUpdateEntry =
    "48 89 5C 24 10 56 48 81 EC 80 00 00 00 "
    "44 0F 29 44 24 50 45 0F 57 C0 33 F6 0F 29 7C 24 60 "
    "48 8B D9 0F 28 F9 39 B1 70 04 00 00 0F 84 ?? ?? ?? ?? "
    "E8 ?? ?? ?? ?? 0F 28 CF 48 8B CB E8 ?? ?? ?? ??";

} // namespace

std::string_view LuxPropOnUpdateEntryPattern() noexcept {
    return kLuxPropOnUpdateEntry;
}

WorldValidationStatus ValidateLuxPropOnUpdate(
    const std::span<const std::uint8_t> code) noexcept {
    if (code.size() < kLuxPropOnUpdateInspectionBytes) {
        return WorldValidationStatus::windowTooSmall;
    }
    const auto pattern = signature::Parse(kLuxPropOnUpdateEntry);
    if (pattern.status != signature::ParseStatus::valid) {
        return WorldValidationStatus::malformedPattern;
    }
    const auto match = signature::FindUnique(
        code.first(kLuxPropOnUpdateInspectionBytes), pattern.pattern);
    if (match.status != signature::MatchStatus::unique || match.offset != 0) {
        return WorldValidationStatus::entryMismatch;
    }
    return WorldValidationStatus::valid;
}

bool ValidatePropBodyVector(const PropBodyVector& vector,
                            std::size_t& count) noexcept {
    count = 0;
    const auto begin = reinterpret_cast<std::uintptr_t>(vector.begin);
    const auto end = reinterpret_cast<std::uintptr_t>(vector.end);
    const auto capacity = reinterpret_cast<std::uintptr_t>(vector.capacity);
    if (begin == 0) {
        return end == 0 && capacity == 0;
    }
    if (end < begin || capacity < end) return false;
    if ((begin % alignof(void*)) != 0 || (end % alignof(void*)) != 0 ||
        (capacity % alignof(void*)) != 0) {
        return false;
    }
    const std::uintptr_t bytes = end - begin;
    const std::uintptr_t capacityBytes = capacity - begin;
    if ((bytes % sizeof(void*)) != 0 ||
        (capacityBytes % sizeof(void*)) != 0) {
        return false;
    }
    const std::uintptr_t rawCount = bytes / sizeof(void*);
    const std::uintptr_t rawCapacity = capacityBytes / sizeof(void*);
    if (rawCount > kMaximumLuxPropBodies ||
        rawCapacity > kMaximumLuxPropBodies || rawCount > rawCapacity ||
        rawCount > (std::numeric_limits<std::size_t>::max)()) {
        return false;
    }
    count = static_cast<std::size_t>(rawCount);
    return true;
}

std::string_view Describe(const WorldValidationStatus status) noexcept {
    switch (status) {
    case WorldValidationStatus::valid: return "valid";
    case WorldValidationStatus::windowTooSmall:
        return "inspection window is too small";
    case WorldValidationStatus::malformedPattern:
        return "internal pattern is malformed";
    case WorldValidationStatus::entryMismatch:
        return "cLuxProp::OnUpdate entry does not match";
    }
    return "unknown world mapping validation status";
}

} // namespace soma::mapping
