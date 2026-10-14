#include "gui_mouse_mapping.h"
#include "signature_scan.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int g_failures = 0;

void Expect(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

void Insert(std::vector<std::uint8_t>& code, const std::string_view text) {
    const auto parsed = soma::signature::Parse(text);
    if (parsed.status != soma::signature::ParseStatus::valid ||
        parsed.pattern.bytes.size() > code.size()) {
        ++g_failures;
        return;
    }
    for (std::size_t index = 0; index < parsed.pattern.bytes.size(); ++index) {
        const auto byte = parsed.pattern.bytes[index];
        code[index] = byte.wildcard ? 0 : byte.value;
    }
}

} // namespace

int main() {
    using soma::mapping::GuiMouseValidationStatus;
    static_assert(soma::mapping::kGuiMouseCurrentPositionOffset == 0x4f3c);
    static_assert(soma::mapping::kGuiMousePreviousPositionOffset == 0x4f44);
    static_assert(soma::mapping::kGuiDrawCommandsOffset == 0x310);
    static_assert(soma::mapping::kGuiDrawCommandSize == 0x98);
    static_assert(soma::mapping::kGuiDrawCommandPositionOffset == 0x08);
    static_assert(soma::mapping::kGuiCursorAlreadyQueuedOffset == 0x5cc);
    static_assert(soma::mapping::kGuiDrawMouseOffset == 0x750);
    static_assert(soma::mapping::kGuiMouseMovementEnabledOffset == 0x751);

    std::vector<std::uint8_t> pixel(
        soma::mapping::kGuiMousePixelSetterInspectionBytes, 0xcc);
    Insert(pixel, soma::mapping::GuiMousePixelSetterEntryPattern());
    Expect(soma::mapping::ValidateGuiMousePixelSetter(pixel) ==
               GuiMouseValidationStatus::valid,
           "exact pixel setter should validate");
    pixel[12] = 0x3d;
    Expect(soma::mapping::ValidateGuiMousePixelSetter(pixel) ==
               GuiMouseValidationStatus::entryMismatch,
           "different current-position offset should fail closed");

    std::vector<std::uint8_t> virtualPosition(
        soma::mapping::kGuiMouseVirtualSetterInspectionBytes, 0xcc);
    Insert(virtualPosition,
           soma::mapping::GuiMouseVirtualSetterEntryPattern());
    Expect(soma::mapping::ValidateGuiMouseVirtualSetter(virtualPosition) ==
               GuiMouseValidationStatus::valid,
           "exact virtual setter should validate");
    virtualPosition[8] = 0x45;
    Expect(soma::mapping::ValidateGuiMouseVirtualSetter(virtualPosition) ==
               GuiMouseValidationStatus::entryMismatch,
           "different previous-position offset should fail closed");

    std::vector<std::uint8_t> cursorQueue(
        soma::mapping::kGuiCursorQueueInspectionBytes, 0xcc);
    Insert(cursorQueue, soma::mapping::GuiCursorQueueEntryPattern());
    Expect(soma::mapping::ValidateGuiCursorQueue(cursorQueue) ==
               GuiMouseValidationStatus::valid,
           "exact cursor queue should validate");
    cursorQueue[16] = 0x52;
    Expect(soma::mapping::ValidateGuiCursorQueue(cursorQueue) ==
               GuiMouseValidationStatus::entryMismatch,
           "different cursor visibility offset should fail closed");

    std::vector<std::uint8_t> guiDoMouse(
        soma::mapping::kGuiDoMouseInspectionBytes, 0xcc);
    Insert(guiDoMouse, soma::mapping::GuiDoMouseEntryPattern());
    Expect(soma::mapping::ValidateGuiDoMouse(guiDoMouse) ==
               GuiMouseValidationStatus::valid,
           "exact cImGui DoMouse should validate");
    guiDoMouse[20] = 0x52;
    Expect(soma::mapping::ValidateGuiDoMouse(guiDoMouse) ==
               GuiMouseValidationStatus::entryMismatch,
           "different cImGui DoMouse movement gate should fail closed");

    std::vector<std::uint8_t> shortCode(32, 0xcc);
    Expect(soma::mapping::ValidateGuiMousePixelSetter(shortCode) ==
               GuiMouseValidationStatus::windowTooSmall,
           "short pixel-setter window should fail closed");
    Expect(soma::mapping::ValidateGuiMouseVirtualSetter(shortCode) ==
               GuiMouseValidationStatus::windowTooSmall,
           "short virtual-setter window should fail closed");
    Expect(soma::mapping::ValidateGuiCursorQueue(shortCode) ==
               GuiMouseValidationStatus::windowTooSmall,
           "short cursor-queue window should fail closed");
    Expect(soma::mapping::ValidateGuiDoMouse(shortCode) ==
               GuiMouseValidationStatus::windowTooSmall,
           "short cImGui DoMouse window should fail closed");

    if (g_failures != 0) return 1;
    std::cout << "GUI mouse mapping tests passed\n";
    return 0;
}
