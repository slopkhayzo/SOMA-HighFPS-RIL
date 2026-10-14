#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace soma::mapping {

inline constexpr std::size_t kHandsSetMatrixInspectionBytes = 0x40;
inline constexpr std::size_t kAttachmentUpdateInspectionBytes = 0x40;
inline constexpr std::size_t kMeshUpdateGraphicsInspectionBytes = 0x40;
inline constexpr std::size_t kSkinningConvertInspectionBytes = 0x30;
inline constexpr std::size_t kSubMeshGetModelMatrixInspectionBytes = 0x10;
// iLuxEntity::GetName() is `lea rax,[rcx+120h]; ret` in the supported build.
inline constexpr std::size_t kLuxEntityNameOffset = 0x120;
inline constexpr std::size_t kMeshBoneMatricesOffset = 0x340;
inline constexpr std::size_t kMeshSkinningVectorsOffset = 0x360;
inline constexpr std::size_t kSubMeshOwnerOffset = 0x1c8;

enum class HandsValidationStatus {
    valid,
    windowTooSmall,
    malformedPattern,
    entryMismatch,
};

[[nodiscard]] std::string_view HandsSetMatrixEntryPattern() noexcept;
[[nodiscard]] std::string_view AttachmentUpdateEntryPattern() noexcept;
[[nodiscard]] std::string_view MeshUpdateGraphicsEntryPattern() noexcept;
[[nodiscard]] std::string_view SkinningConvertEntryPattern() noexcept;
[[nodiscard]] std::string_view SubMeshGetModelMatrixEntryPattern() noexcept;
[[nodiscard]] HandsValidationStatus ValidateHandsSetMatrix(
    std::span<const std::uint8_t> code) noexcept;
[[nodiscard]] HandsValidationStatus ValidateAttachmentUpdate(
    std::span<const std::uint8_t> code) noexcept;
[[nodiscard]] HandsValidationStatus ValidateMeshUpdateGraphics(
    std::span<const std::uint8_t> code) noexcept;
[[nodiscard]] HandsValidationStatus ValidateSkinningConvert(
    std::span<const std::uint8_t> code) noexcept;
[[nodiscard]] HandsValidationStatus ValidateSubMeshGetModelMatrix(
    std::span<const std::uint8_t> code) noexcept;
[[nodiscard]] std::string_view Describe(HandsValidationStatus status) noexcept;
[[nodiscard]] bool IsHandsHudObjectName(std::string_view name) noexcept;
[[nodiscard]] bool IsHandsAttachmentObjectName(std::string_view name) noexcept;

} // namespace soma::mapping
