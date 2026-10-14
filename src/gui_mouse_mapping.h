#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace soma::mapping {

inline constexpr std::size_t kGuiMousePixelSetterInspectionBytes = 0x40;
inline constexpr std::size_t kGuiMouseVirtualSetterInspectionBytes = 0x40;
inline constexpr std::size_t kGuiCursorQueueInspectionBytes = 0x40;
inline constexpr std::size_t kGuiDoMouseInspectionBytes = 0x40;
inline constexpr std::size_t kGuiMouseCurrentPositionOffset = 0x4f3c;
inline constexpr std::size_t kGuiMousePreviousPositionOffset = 0x4f44;
inline constexpr std::size_t kGuiDrawCommandsOffset = 0x310;
inline constexpr std::size_t kGuiDrawCommandSize = 0x98;
inline constexpr std::size_t kGuiDrawCommandPositionOffset = 0x08;
inline constexpr std::size_t kGuiCursorAlreadyQueuedOffset = 0x5cc;
inline constexpr std::size_t kGuiDrawMouseOffset = 0x750;
inline constexpr std::size_t kGuiMouseMovementEnabledOffset = 0x751;

enum class GuiMouseValidationStatus {
    valid,
    windowTooSmall,
    malformedPattern,
    entryMismatch,
};

[[nodiscard]] std::string_view GuiMousePixelSetterEntryPattern() noexcept;
[[nodiscard]] std::string_view GuiMouseVirtualSetterEntryPattern() noexcept;
[[nodiscard]] std::string_view GuiCursorQueueEntryPattern() noexcept;
[[nodiscard]] std::string_view GuiDoMouseEntryPattern() noexcept;
[[nodiscard]] GuiMouseValidationStatus ValidateGuiMousePixelSetter(
    std::span<const std::uint8_t> code) noexcept;
[[nodiscard]] GuiMouseValidationStatus ValidateGuiMouseVirtualSetter(
    std::span<const std::uint8_t> code) noexcept;
[[nodiscard]] GuiMouseValidationStatus ValidateGuiCursorQueue(
    std::span<const std::uint8_t> code) noexcept;
[[nodiscard]] GuiMouseValidationStatus ValidateGuiDoMouse(
    std::span<const std::uint8_t> code) noexcept;
[[nodiscard]] std::string_view Describe(GuiMouseValidationStatus status) noexcept;

} // namespace soma::mapping
