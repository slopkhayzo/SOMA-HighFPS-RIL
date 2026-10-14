#include "gui_mouse_mapping.h"

#include "signature_scan.h"

namespace soma::mapping {
namespace {

// cImGui::SendMousePosition. The stock method copies mvMousePosition to
// mvMousePrevPosition, converts pixel position/delta through the active
// graphics and virtual-GUI sizes, and stores mvMouseRel.
constexpr std::string_view kGuiMousePixelSetterEntry =
    "48 89 5C 24 10 57 48 83 EC 40 8B 81 3C 4F 00 00 "
    "0F 29 74 24 30 48 8B F9 89 81 44 4F 00 00 "
    "8B 81 40 4F 00 00 0F 29 7C 24 20 89 81 48 4F 00 00 "
    "48 8B 41 28 49 8B D8 66 0F 6E 32";

// cImGui::SendMouseVirtualPosition. This overload receives already-converted
// float position/delta pairs and writes the same current/previous/relative
// fields as the pixel setter.
constexpr std::string_view kGuiMouseVirtualSetterEntry =
    "8B 81 3C 4F 00 00 89 81 44 4F 00 00 "
    "8B 81 40 4F 00 00 89 81 48 4F 00 00 "
    "8B 02 89 81 3C 4F 00 00 8B 42 04 89 81 40 4F 00 00 "
    "41 8B 00 89 81 4C 4F 00 00 41 8B 40 04 89 81 50 4F 00 00";

// cImGui::Draw. The cursor is the first graphics command this method queues
// when drawing and mouse movement are enabled. The entry also validates the
// three gating fields used by the runtime before it records that command.
constexpr std::string_view kGuiCursorQueueEntry =
    "48 8B C4 41 54 48 81 EC D0 00 00 00 "
    "80 B9 51 07 00 00 00 4C 8B E1 C6 81 CD 05 00 00 00 "
    "0F 84 49 01 00 00 80 B9 50 07 00 00 00 "
    "0F 84 3C 01 00 00 80 B9 CC 05 00 00 00 "
    "0F 85 2F 01 00 00 48 89 58";

// cImGui::DoMouse. Script-driven menus use this path to queue a custom mouse
// command before cImGui::Draw and then set the already-queued flag at +0x5CC.
constexpr std::string_view kGuiDoMouseEntry =
    "48 8B C4 41 54 41 55 41 56 48 81 EC C0 00 00 00 "
    "80 B9 51 07 00 00 00 4D 8B E9 4C 8B F2 4C 8B E1 "
    "0F 84 14 01 00 00 F3 0F 10 89 48 4F 00 00 "
    "F3 0F 10 91 54 07 00 00 F3 0F 10 81 44 4F 00 00";

GuiMouseValidationStatus Validate(
    const std::span<const std::uint8_t> code,
    const std::size_t inspectionBytes,
    const std::string_view entry) noexcept {
    if (code.size() < inspectionBytes) {
        return GuiMouseValidationStatus::windowTooSmall;
    }
    const auto pattern = signature::Parse(entry);
    if (pattern.status != signature::ParseStatus::valid) {
        return GuiMouseValidationStatus::malformedPattern;
    }
    const auto match = signature::FindUnique(
        code.first(inspectionBytes), pattern.pattern);
    if (match.status != signature::MatchStatus::unique || match.offset != 0) {
        return GuiMouseValidationStatus::entryMismatch;
    }
    return GuiMouseValidationStatus::valid;
}

} // namespace

std::string_view GuiMousePixelSetterEntryPattern() noexcept {
    return kGuiMousePixelSetterEntry;
}

std::string_view GuiMouseVirtualSetterEntryPattern() noexcept {
    return kGuiMouseVirtualSetterEntry;
}

std::string_view GuiCursorQueueEntryPattern() noexcept {
    return kGuiCursorQueueEntry;
}

std::string_view GuiDoMouseEntryPattern() noexcept {
    return kGuiDoMouseEntry;
}

GuiMouseValidationStatus ValidateGuiMousePixelSetter(
    const std::span<const std::uint8_t> code) noexcept {
    return Validate(code, kGuiMousePixelSetterInspectionBytes,
                    kGuiMousePixelSetterEntry);
}

GuiMouseValidationStatus ValidateGuiMouseVirtualSetter(
    const std::span<const std::uint8_t> code) noexcept {
    return Validate(code, kGuiMouseVirtualSetterInspectionBytes,
                    kGuiMouseVirtualSetterEntry);
}

GuiMouseValidationStatus ValidateGuiCursorQueue(
    const std::span<const std::uint8_t> code) noexcept {
    return Validate(code, kGuiCursorQueueInspectionBytes,
                    kGuiCursorQueueEntry);
}

GuiMouseValidationStatus ValidateGuiDoMouse(
    const std::span<const std::uint8_t> code) noexcept {
    return Validate(code, kGuiDoMouseInspectionBytes, kGuiDoMouseEntry);
}

std::string_view Describe(const GuiMouseValidationStatus status) noexcept {
    switch (status) {
    case GuiMouseValidationStatus::valid: return "valid";
    case GuiMouseValidationStatus::windowTooSmall:
        return "inspection window is too small";
    case GuiMouseValidationStatus::malformedPattern:
        return "internal pattern is malformed";
    case GuiMouseValidationStatus::entryMismatch:
        return "cImGui mouse-setter entry does not match";
    }
    return "unknown GUI mouse mapping validation status";
}

} // namespace soma::mapping
