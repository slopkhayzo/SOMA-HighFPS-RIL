#include "hands_mapping.h"
#include "signature_scan.h"

#include <algorithm>
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

void Insert(std::vector<std::uint8_t>& code, const std::size_t offset,
            const std::string_view text) {
    const auto parsed = soma::signature::Parse(text);
    if (parsed.status != soma::signature::ParseStatus::valid ||
        offset + parsed.pattern.bytes.size() > code.size()) {
        ++g_failures;
        return;
    }
    for (std::size_t index = 0; index < parsed.pattern.bytes.size(); ++index) {
        const auto byte = parsed.pattern.bytes[index];
        code[offset + index] = byte.wildcard ? 0 : byte.value;
    }
}

} // namespace

int main() {
    using soma::mapping::HandsValidationStatus;
    static_assert(soma::mapping::kLuxEntityNameOffset == 0x120);
    static_assert(soma::mapping::kSubMeshOwnerOffset == 0x1c8);
    static_assert(soma::mapping::kMeshSkinningVectorsOffset == 0x360);
    std::vector<std::uint8_t> code(
        soma::mapping::kHandsSetMatrixInspectionBytes, 0xcc);
    Insert(code, 0, soma::mapping::HandsSetMatrixEntryPattern());
    Expect(soma::mapping::ValidateHandsSetMatrix(code) ==
               HandsValidationStatus::valid,
           "exact setter entry should validate");

    auto changedVirtualSlot = code;
    changedVirtualSlot[24] = 0x48;
    Expect(soma::mapping::ValidateHandsSetMatrix(changedVirtualSlot) ==
               HandsValidationStatus::entryMismatch,
           "different virtual setter slot should fail closed");

    std::vector<std::uint8_t> shortCode(32, 0xcc);
    Expect(soma::mapping::ValidateHandsSetMatrix(shortCode) ==
               HandsValidationStatus::windowTooSmall,
           "short setter window should fail closed");

    std::vector<std::uint8_t> attachmentCode(
        soma::mapping::kAttachmentUpdateInspectionBytes, 0xcc);
    Insert(attachmentCode, 0,
           soma::mapping::AttachmentUpdateEntryPattern());
    Expect(soma::mapping::ValidateAttachmentUpdate(attachmentCode) ==
               HandsValidationStatus::valid,
           "exact attachment update entry should validate");
    attachmentCode[25] = 0x41;
    Expect(soma::mapping::ValidateAttachmentUpdate(attachmentCode) ==
               HandsValidationStatus::entryMismatch,
           "different attachment-state field should fail closed");

    std::vector<std::uint8_t> meshCode(
        soma::mapping::kMeshUpdateGraphicsInspectionBytes, 0xcc);
    Insert(meshCode, 0, soma::mapping::MeshUpdateGraphicsEntryPattern());
    Expect(soma::mapping::ValidateMeshUpdateGraphics(meshCode) ==
               HandsValidationStatus::valid,
           "exact mesh graphics entry should validate");
    auto changedPaletteGate = meshCode;
    changedPaletteGate[11] = 0x98;
    Expect(soma::mapping::ValidateMeshUpdateGraphics(changedPaletteGate) ==
               HandsValidationStatus::entryMismatch,
           "different mesh palette dirty field should fail closed");

    std::vector<std::uint8_t> conversionCode(
        soma::mapping::kSkinningConvertInspectionBytes, 0xcc);
    Insert(conversionCode, 0,
           soma::mapping::SkinningConvertEntryPattern());
    Expect(soma::mapping::ValidateSkinningConvert(conversionCode) ==
               HandsValidationStatus::valid,
           "exact skinning conversion entry should validate");
    conversionCode[19] = 0x10;
    Expect(soma::mapping::ValidateSkinningConvert(conversionCode) ==
               HandsValidationStatus::entryMismatch,
           "different source-matrix field should fail closed");

    std::vector<std::uint8_t> subMeshCode(
        soma::mapping::kSubMeshGetModelMatrixInspectionBytes, 0xcc);
    Insert(subMeshCode, 0,
           soma::mapping::SubMeshGetModelMatrixEntryPattern());
    Expect(soma::mapping::ValidateSubMeshGetModelMatrix(subMeshCode) ==
               HandsValidationStatus::valid,
           "exact submesh model-matrix entry should validate");
    subMeshCode[3] = 0x71;
    Expect(soma::mapping::ValidateSubMeshGetModelMatrix(subMeshCode) ==
               HandsValidationStatus::entryMismatch,
           "different Entity3D base offset should fail closed");

    Expect(soma::mapping::IsHandsHudObjectName("HudObject"),
           "generic interaction HUD object should be recognized");
    Expect(soma::mapping::IsHandsHudObjectName("Tracer_Fluid_HudObject"),
           "attached held HUD object should be recognized");
    Expect(soma::mapping::IsHandsHudObjectName("Catherine_Tool_HudObject"),
           "Omnitool HUD object should be recognized");
    Expect(!soma::mapping::IsHandsHudObjectName("HudObjectPreview"),
           "unrelated HUD-object prefix should not be recognized");
    Expect(!soma::mapping::IsHandsHudObjectName("WorldProp"),
           "ordinary world prop should not be recognized");
    Expect(soma::mapping::IsHandsAttachmentObjectName(
               "Tracer_Fluid_HudObject"),
           "existing HUD objects should remain attachment candidates");
    Expect(soma::mapping::IsHandsAttachmentObjectName("Cellphone_open"),
           "spawned handheld-terminal HUD object should be recognized");
    Expect(soma::mapping::IsHandsAttachmentObjectName("Datapad_open"),
           "other handheld-terminal HUD objects should be recognized");
    Expect(!soma::mapping::IsHandsAttachmentObjectName("Cellphone"),
           "world handheld-terminal source should remain excluded");
    Expect(!soma::mapping::IsHandsAttachmentObjectName("open"),
           "unrelated names should remain excluded");

    if (g_failures != 0) return 1;
    std::cout << "hands mapping tests passed\n";
    return 0;
}
