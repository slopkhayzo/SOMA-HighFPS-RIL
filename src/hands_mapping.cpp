#include "hands_mapping.h"

#include "signature_scan.h"

namespace soma::mapping {
namespace {

constexpr std::string_view kHandsSetMatrixEntry =
    "40 53 48 81 EC A0 00 00 00 48 8B 01 48 8B D9 "
    "C6 81 62 04 00 00 01 FF 90 40 01 00 00 48 8B CB "
    "E8 ?? ?? ?? ?? 48 8D 05 ?? ?? ?? ?? 48 8D 8B 20 07 00 00";

constexpr std::string_view kAttachmentUpdateEntry =
    "40 55 56 48 8D AC 24 88 FD FF FF 48 81 EC 78 03 00 00 "
    "48 8B F1 48 8B 89 40 07 00 00 48 85 C9 0F 84 ?? ?? ?? ?? "
    "0F B6 05 ?? ?? ?? ?? 38 81 88 00 00 00";

constexpr std::string_view kMeshUpdateGraphicsEntry =
    "40 56 48 81 EC C0 00 00 00 80 B9 99 01 00 00 00 "
    "48 8B F1 75 11 E8 ?? ?? ?? ?? 39 86 9C 01 00 00 "
    "0F 84 ?? ?? ?? ?? 48 8B CE 4C 89 A4 24 B8 00 00 00 "
    "E8 ?? ?? ?? ?? 48 8B 8E 18 04 00 00";

constexpr std::string_view kSkinningConvertEntry =
    "48 89 5C 24 08 57 48 83 EC 50 0F 29 74 24 40 "
    "F3 0F 10 71 0C 48 8B DA 0F 29 7C 24 30 "
    "F3 0F 10 79 2C 48 8B D1 49 8B F8";

constexpr std::string_view kSubMeshGetModelMatrixEntry =
    "48 83 C1 70 E9 D7 59 11 00 CC CC CC CC CC CC CC";

} // namespace

std::string_view HandsSetMatrixEntryPattern() noexcept {
    return kHandsSetMatrixEntry;
}

std::string_view AttachmentUpdateEntryPattern() noexcept {
    return kAttachmentUpdateEntry;
}

std::string_view MeshUpdateGraphicsEntryPattern() noexcept {
    return kMeshUpdateGraphicsEntry;
}

std::string_view SkinningConvertEntryPattern() noexcept {
    return kSkinningConvertEntry;
}

std::string_view SubMeshGetModelMatrixEntryPattern() noexcept {
    return kSubMeshGetModelMatrixEntry;
}

HandsValidationStatus ValidateHandsSetMatrix(
    const std::span<const std::uint8_t> code) noexcept {
    if (code.size() < kHandsSetMatrixInspectionBytes) {
        return HandsValidationStatus::windowTooSmall;
    }
    const auto pattern = signature::Parse(kHandsSetMatrixEntry);
    if (pattern.status != signature::ParseStatus::valid) {
        return HandsValidationStatus::malformedPattern;
    }
    const auto match = signature::FindUnique(
        code.first(kHandsSetMatrixInspectionBytes), pattern.pattern);
    if (match.status != signature::MatchStatus::unique || match.offset != 0) {
        return HandsValidationStatus::entryMismatch;
    }
    return HandsValidationStatus::valid;
}

HandsValidationStatus ValidateAttachmentUpdate(
    const std::span<const std::uint8_t> code) noexcept {
    if (code.size() < kAttachmentUpdateInspectionBytes) {
        return HandsValidationStatus::windowTooSmall;
    }
    const auto pattern = signature::Parse(kAttachmentUpdateEntry);
    if (pattern.status != signature::ParseStatus::valid) {
        return HandsValidationStatus::malformedPattern;
    }
    const auto match = signature::FindUnique(
        code.first(kAttachmentUpdateInspectionBytes), pattern.pattern);
    if (match.status != signature::MatchStatus::unique || match.offset != 0) {
        return HandsValidationStatus::entryMismatch;
    }
    return HandsValidationStatus::valid;
}

HandsValidationStatus ValidateMeshUpdateGraphics(
    const std::span<const std::uint8_t> code) noexcept {
    if (code.size() < kMeshUpdateGraphicsInspectionBytes) {
        return HandsValidationStatus::windowTooSmall;
    }
    const auto pattern = signature::Parse(kMeshUpdateGraphicsEntry);
    if (pattern.status != signature::ParseStatus::valid) {
        return HandsValidationStatus::malformedPattern;
    }
    const auto match = signature::FindUnique(
        code.first(kMeshUpdateGraphicsInspectionBytes), pattern.pattern);
    if (match.status != signature::MatchStatus::unique || match.offset != 0) {
        return HandsValidationStatus::entryMismatch;
    }
    return HandsValidationStatus::valid;
}

HandsValidationStatus ValidateSkinningConvert(
    const std::span<const std::uint8_t> code) noexcept {
    if (code.size() < kSkinningConvertInspectionBytes) {
        return HandsValidationStatus::windowTooSmall;
    }
    const auto pattern = signature::Parse(kSkinningConvertEntry);
    if (pattern.status != signature::ParseStatus::valid) {
        return HandsValidationStatus::malformedPattern;
    }
    const auto match = signature::FindUnique(
        code.first(kSkinningConvertInspectionBytes), pattern.pattern);
    if (match.status != signature::MatchStatus::unique || match.offset != 0) {
        return HandsValidationStatus::entryMismatch;
    }
    return HandsValidationStatus::valid;
}

HandsValidationStatus ValidateSubMeshGetModelMatrix(
    const std::span<const std::uint8_t> code) noexcept {
    if (code.size() < kSubMeshGetModelMatrixInspectionBytes) {
        return HandsValidationStatus::windowTooSmall;
    }
    const auto pattern = signature::Parse(kSubMeshGetModelMatrixEntry);
    if (pattern.status != signature::ParseStatus::valid) {
        return HandsValidationStatus::malformedPattern;
    }
    const auto match = signature::FindUnique(
        code.first(kSubMeshGetModelMatrixInspectionBytes), pattern.pattern);
    if (match.status != signature::MatchStatus::unique || match.offset != 0) {
        return HandsValidationStatus::entryMismatch;
    }
    return HandsValidationStatus::valid;
}

std::string_view Describe(const HandsValidationStatus status) noexcept {
    switch (status) {
    case HandsValidationStatus::valid: return "valid";
    case HandsValidationStatus::windowTooSmall:
        return "inspection window is too small";
    case HandsValidationStatus::malformedPattern:
        return "internal pattern is malformed";
    case HandsValidationStatus::entryMismatch:
        return "iLuxEntity::SetMatrix entry does not match";
    }
    return "unknown hands mapping validation status";
}

bool IsHandsHudObjectName(const std::string_view name) noexcept {
    constexpr std::string_view kExact = "HudObject";
    constexpr std::string_view kSuffix = "_HudObject";
    return name == kExact ||
           (name.size() >= kSuffix.size() &&
            name.substr(name.size() - kSuffix.size()) == kSuffix);
}

bool IsHandsAttachmentObjectName(const std::string_view name) noexcept {
    constexpr std::string_view kHandheldTerminalSuffix = "_open";
    return IsHandsHudObjectName(name) ||
           (name.size() >= kHandheldTerminalSuffix.size() &&
            name.substr(name.size() - kHandheldTerminalSuffix.size()) ==
                kHandheldTerminalSuffix);
}

} // namespace soma::mapping
