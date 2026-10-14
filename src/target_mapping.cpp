#include "target_mapping.h"

#include "agent_mapping.h"
#include "engine_mapping.h"
#include "gui_mouse_mapping.h"
#include "hands_mapping.h"
#include "scene_mapping.h"
#include "signature_scan.h"
#include "world_mapping.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace soma::target {
namespace {

struct ExecutableRegion {
    const std::uint8_t* begin = nullptr;
    std::size_t size = 0;
};

struct LocatedPattern {
    signature::MatchStatus status = signature::MatchStatus::noMatch;
    const std::uint8_t* address = nullptr;
    const std::uint8_t* regionEnd = nullptr;
};

const IMAGE_NT_HEADERS64* NtHeaders(const HMODULE executable) noexcept {
    if (executable == nullptr) return nullptr;
    const auto base = reinterpret_cast<std::uintptr_t>(executable);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return nullptr;
    return reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        base + static_cast<std::uintptr_t>(dos->e_lfanew));
}

bool ExecutableRegions(const HMODULE executable,
                       std::vector<ExecutableRegion>& regions) noexcept {
    const auto* nt = NtHeaders(executable);
    if (nt == nullptr || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->FileHeader.NumberOfSections == 0 ||
        nt->FileHeader.NumberOfSections > 96) {
        return false;
    }

    const auto base = reinterpret_cast<std::uintptr_t>(executable);
    const std::uint32_t imageSize = nt->OptionalHeader.SizeOfImage;
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (std::uint16_t index = 0; index < nt->FileHeader.NumberOfSections;
         ++index, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        const std::uint32_t virtualAddress = section->VirtualAddress;
        const std::uint32_t requestedSize =
            section->Misc.VirtualSize != 0 ? section->Misc.VirtualSize
                                           : section->SizeOfRawData;
        if (virtualAddress >= imageSize || requestedSize == 0) continue;
        const std::uint32_t available = imageSize - virtualAddress;
        const std::uint32_t regionSize =
            (std::min)(requestedSize, available);
        regions.push_back(ExecutableRegion{
            reinterpret_cast<const std::uint8_t*>(base + virtualAddress),
            static_cast<std::size_t>(regionSize)});
    }
    return !regions.empty();
}

LocatedPattern FindAcrossRegions(const std::vector<ExecutableRegion>& regions,
                                 const signature::Pattern& pattern) noexcept {
    LocatedPattern located{};
    for (const auto& region : regions) {
        const auto match = signature::FindUnique(
            std::span<const std::uint8_t>(region.begin, region.size), pattern);
        if (match.status == signature::MatchStatus::ambiguous) {
            located.status = signature::MatchStatus::ambiguous;
            return located;
        }
        if (match.status != signature::MatchStatus::unique) continue;
        if (located.status == signature::MatchStatus::unique) {
            located.status = signature::MatchStatus::ambiguous;
            located.address = nullptr;
            located.regionEnd = nullptr;
            return located;
        }
        located.status = signature::MatchStatus::unique;
        located.address = region.begin + match.offset;
        located.regionEnd = region.begin + region.size;
    }
    return located;
}

bool IsInExecutableRegion(const std::vector<ExecutableRegion>& regions,
                          const std::uintptr_t address) noexcept {
    for (const auto& region : regions) {
        const auto begin = reinterpret_cast<std::uintptr_t>(region.begin);
        const auto end = begin + region.size;
        if (address >= begin && address < end) return true;
    }
    return false;
}

std::span<const std::uint8_t> ExecutableBytesAt(
    const std::vector<ExecutableRegion>& regions,
    const std::uintptr_t address) noexcept {
    for (const auto& region : regions) {
        const auto begin = reinterpret_cast<std::uintptr_t>(region.begin);
        const auto end = begin + region.size;
        if (address >= begin && address < end) {
            return {reinterpret_cast<const std::uint8_t*>(address),
                    static_cast<std::size_t>(end - address)};
        }
    }
    return {};
}

std::uintptr_t DecodeRelativeCall(const std::uint8_t* const call) noexcept {
    std::int32_t displacement = 0;
    std::memcpy(&displacement, call + 1, sizeof(displacement));
    const auto base = reinterpret_cast<std::intptr_t>(call + 5);
    return static_cast<std::uintptr_t>(
        base + static_cast<std::intptr_t>(displacement));
}

std::wstring Widen(const std::string_view text) {
    return std::wstring(text.begin(), text.end());
}

} // namespace

InspectionResult InspectMappedHost(const HMODULE executable) noexcept {
    InspectionResult result{};
    if (executable == nullptr) {
        result.error = L"Host module handle is null.";
        return result;
    }

    const auto base = reinterpret_cast<std::uintptr_t>(executable);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        result.error = L"Host does not contain a valid DOS header.";
        return result;
    }
    if (static_cast<std::uint64_t>(dos->e_lfanew) >
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        result.error = L"Host PE header offset is out of range.";
        return result;
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        base + static_cast<std::uintptr_t>(dos->e_lfanew));
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        result.error = L"Host is not a valid PE32+ image.";
        return result;
    }
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
        result.error = L"Host is not an AMD64 executable.";
        return result;
    }
    if (nt->OptionalHeader.SizeOfImage < nt->OptionalHeader.SizeOfHeaders ||
        nt->OptionalHeader.SizeOfCode == 0) {
        result.error = L"Host PE image sizes are invalid.";
        return result;
    }

    result.identity.machine = nt->FileHeader.Machine;
    result.identity.timestamp = nt->FileHeader.TimeDateStamp;
    result.identity.imageSize = nt->OptionalHeader.SizeOfImage;
    result.identity.codeSize = nt->OptionalHeader.SizeOfCode;
    result.identity.imageBase = base;
    result.ok = true;
    return result;
}

MappingResult ResolveRuntimeMappings(const HMODULE executable) noexcept {
    MappingResult result{};
    const InspectionResult host = InspectMappedHost(executable);
    if (!host.ok) {
        result.error = L"Host inspection failed: " + host.error;
        return result;
    }

    std::vector<ExecutableRegion> regions;
    if (!ExecutableRegions(executable, regions)) {
        result.error = L"Could not enumerate executable PE sections.";
        return result;
    }

    const auto entryPattern = signature::Parse(mapping::EngineRunEntryPattern());
    if (entryPattern.status != signature::ParseStatus::valid) {
        result.error = L"Internal cEngine::Run signature is malformed.";
        return result;
    }
    const LocatedPattern engineRun =
        FindAcrossRegions(regions, entryPattern.pattern);
    if (engineRun.status == signature::MatchStatus::noMatch) {
        result.error = L"cEngine::Run signature was not found.";
        return result;
    }
    if (engineRun.status == signature::MatchStatus::ambiguous) {
        result.error = L"cEngine::Run signature was ambiguous.";
        return result;
    }

    const std::size_t available = static_cast<std::size_t>(
        engineRun.regionEnd - engineRun.address);
    if (available < mapping::kEngineRunInspectionBytes) {
        result.error = L"cEngine::Run is too close to the executable-section boundary.";
        return result;
    }
    const mapping::EngineRunEvidence evidence = mapping::ValidateEngineRun(
        std::span<const std::uint8_t>(engineRun.address,
                                      mapping::kEngineRunInspectionBytes));
    if (evidence.status != mapping::ValidationStatus::valid) {
        result.error = L"cEngine::Run structural validation failed: " +
                       Widen(mapping::Describe(evidence.status));
        return result;
    }

    const auto entryAddress = reinterpret_cast<std::uintptr_t>(engineRun.address);
    const std::uintptr_t dispatchAddress = DecodeRelativeCall(
        engineRun.address + evidence.updateDispatchCallOffset);
    if (!IsInExecutableRegion(regions, dispatchAddress)) {
        result.error = L"cUpdater::RunMessage target is outside executable sections.";
        return result;
    }

    const std::uintptr_t clockAddress = DecodeRelativeCall(
        engineRun.address + evidence.schedulerClockCallOffset);
    const auto clockBytes = ExecutableBytesAt(regions, clockAddress);
    constexpr std::string_view kEngineMicrosecondsPattern =
        "48 83 EC 28 E8 ?? ?? ?? ?? 33 D2 48 2B 05 ?? ?? ?? ?? "
        "48 69 C0 40 42 0F 00 48 F7 35 ?? ?? ?? ?? 48 83 C4 28 C3";
    const auto parsedClock = signature::Parse(kEngineMicrosecondsPattern);
    if (parsedClock.status != signature::ParseStatus::valid ||
        clockBytes.size() < parsedClock.pattern.bytes.size()) {
        result.error = L"Engine microsecond clock target is unavailable.";
        return result;
    }
    const auto clockMatch = signature::FindUnique(
        clockBytes.first(parsedClock.pattern.bytes.size()),
        parsedClock.pattern);
    if (clockMatch.status != signature::MatchStatus::unique ||
        clockMatch.offset != 0) {
        result.error = L"Engine microsecond clock validation failed.";
        return result;
    }

    const std::uintptr_t sceneRenderAddress = DecodeRelativeCall(
        engineRun.address + evidence.sceneRenderCallOffset);
    const auto sceneBytes = ExecutableBytesAt(regions, sceneRenderAddress);
    if (sceneBytes.size() < mapping::kSceneRenderInspectionBytes) {
        result.error = L"cScene::Render target is unavailable.";
        return result;
    }
    const mapping::SceneRenderEvidence sceneEvidence =
        mapping::ValidateSceneRender(
            sceneBytes.first(mapping::kSceneRenderInspectionBytes));
    if (sceneEvidence.status != mapping::SceneValidationStatus::valid) {
        result.error = L"cScene::Render structural validation failed: " +
                       Widen(mapping::Describe(sceneEvidence.status));
        return result;
    }

    const auto* const sceneEntry =
        reinterpret_cast<const std::uint8_t*>(sceneRenderAddress);
    const std::uintptr_t viewportRenderAddress = DecodeRelativeCall(
        sceneEntry + sceneEvidence.viewportRenderCallOffset);
    const auto viewportBytes = ExecutableBytesAt(regions, viewportRenderAddress);
    if (viewportBytes.size() < mapping::kViewportRenderInspectionBytes) {
        result.error = L"Per-viewport render target is unavailable.";
        return result;
    }
    const mapping::ViewportRenderEvidence viewportEvidence =
        mapping::ValidateViewportRender(
            viewportBytes.first(mapping::kViewportRenderInspectionBytes));
    if (viewportEvidence.status != mapping::ViewportValidationStatus::valid) {
        result.error = L"Per-viewport render structural validation failed: " +
                       Widen(mapping::Describe(viewportEvidence.status));
        return result;
    }

    const auto handsPattern =
        signature::Parse(mapping::HandsSetMatrixEntryPattern());
    if (handsPattern.status != signature::ParseStatus::valid) {
        result.error = L"Internal iLuxEntity::SetMatrix signature is malformed.";
        return result;
    }
    const LocatedPattern handsSetMatrix =
        FindAcrossRegions(regions, handsPattern.pattern);
    if (handsSetMatrix.status == signature::MatchStatus::noMatch) {
        result.error = L"iLuxEntity::SetMatrix signature was not found.";
        return result;
    }
    if (handsSetMatrix.status == signature::MatchStatus::ambiguous) {
        result.error = L"iLuxEntity::SetMatrix signature was ambiguous.";
        return result;
    }
    const auto handsAddress =
        reinterpret_cast<std::uintptr_t>(handsSetMatrix.address);
    const auto handsBytes = ExecutableBytesAt(regions, handsAddress);
    if (handsBytes.size() < mapping::kHandsSetMatrixInspectionBytes) {
        result.error = L"iLuxEntity::SetMatrix target is unavailable.";
        return result;
    }
    const mapping::HandsValidationStatus handsStatus =
        mapping::ValidateHandsSetMatrix(
            handsBytes.first(mapping::kHandsSetMatrixInspectionBytes));
    if (handsStatus != mapping::HandsValidationStatus::valid) {
        result.error = L"iLuxEntity::SetMatrix structural validation failed: " +
                       Widen(mapping::Describe(handsStatus));
        return result;
    }

    const auto attachmentPattern =
        signature::Parse(mapping::AttachmentUpdateEntryPattern());
    if (attachmentPattern.status != signature::ParseStatus::valid) {
        result.error = L"Internal iLuxEntity::UpdateEntityAttachment signature is malformed.";
        return result;
    }
    const LocatedPattern attachmentUpdate =
        FindAcrossRegions(regions, attachmentPattern.pattern);
    if (attachmentUpdate.status == signature::MatchStatus::noMatch) {
        result.error = L"iLuxEntity::UpdateEntityAttachment signature was not found.";
        return result;
    }
    if (attachmentUpdate.status == signature::MatchStatus::ambiguous) {
        result.error = L"iLuxEntity::UpdateEntityAttachment signature was ambiguous.";
        return result;
    }
    const auto attachmentAddress =
        reinterpret_cast<std::uintptr_t>(attachmentUpdate.address);
    const auto attachmentBytes = ExecutableBytesAt(regions, attachmentAddress);
    if (attachmentBytes.size() < mapping::kAttachmentUpdateInspectionBytes) {
        result.error = L"iLuxEntity::UpdateEntityAttachment target is unavailable.";
        return result;
    }
    const mapping::HandsValidationStatus attachmentStatus =
        mapping::ValidateAttachmentUpdate(attachmentBytes.first(
            mapping::kAttachmentUpdateInspectionBytes));
    if (attachmentStatus != mapping::HandsValidationStatus::valid) {
        result.error =
            L"iLuxEntity::UpdateEntityAttachment structural validation failed: " +
            Widen(mapping::Describe(attachmentStatus));
        return result;
    }

    const auto meshUpdatePattern =
        signature::Parse(mapping::MeshUpdateGraphicsEntryPattern());
    if (meshUpdatePattern.status != signature::ParseStatus::valid) {
        result.error = L"Internal cMeshEntity::UpdateGraphicsForFrame signature is malformed.";
        return result;
    }
    const LocatedPattern meshUpdateGraphics =
        FindAcrossRegions(regions, meshUpdatePattern.pattern);
    if (meshUpdateGraphics.status == signature::MatchStatus::noMatch) {
        result.error = L"cMeshEntity::UpdateGraphicsForFrame signature was not found.";
        return result;
    }
    if (meshUpdateGraphics.status == signature::MatchStatus::ambiguous) {
        result.error = L"cMeshEntity::UpdateGraphicsForFrame signature was ambiguous.";
        return result;
    }
    const auto meshUpdateAddress =
        reinterpret_cast<std::uintptr_t>(meshUpdateGraphics.address);
    const auto meshUpdateBytes =
        ExecutableBytesAt(regions, meshUpdateAddress);
    if (meshUpdateBytes.size() < mapping::kMeshUpdateGraphicsInspectionBytes) {
        result.error = L"cMeshEntity::UpdateGraphicsForFrame target is unavailable.";
        return result;
    }
    const mapping::HandsValidationStatus meshUpdateStatus =
        mapping::ValidateMeshUpdateGraphics(meshUpdateBytes.first(
            mapping::kMeshUpdateGraphicsInspectionBytes));
    if (meshUpdateStatus != mapping::HandsValidationStatus::valid) {
        result.error =
            L"cMeshEntity::UpdateGraphicsForFrame structural validation failed: " +
            Widen(mapping::Describe(meshUpdateStatus));
        return result;
    }

    const auto conversionPattern =
        signature::Parse(mapping::SkinningConvertEntryPattern());
    if (conversionPattern.status != signature::ParseStatus::valid) {
        result.error = L"Internal skinning-palette conversion signature is malformed.";
        return result;
    }
    const LocatedPattern skinningConvert =
        FindAcrossRegions(regions, conversionPattern.pattern);
    if (skinningConvert.status == signature::MatchStatus::noMatch) {
        result.error = L"Skinning-palette conversion signature was not found.";
        return result;
    }
    if (skinningConvert.status == signature::MatchStatus::ambiguous) {
        result.error = L"Skinning-palette conversion signature was ambiguous.";
        return result;
    }
    const auto skinningConvertAddress =
        reinterpret_cast<std::uintptr_t>(skinningConvert.address);
    const auto conversionBytes =
        ExecutableBytesAt(regions, skinningConvertAddress);
    if (conversionBytes.size() < mapping::kSkinningConvertInspectionBytes) {
        result.error = L"Skinning-palette conversion target is unavailable.";
        return result;
    }
    const mapping::HandsValidationStatus conversionStatus =
        mapping::ValidateSkinningConvert(conversionBytes.first(
            mapping::kSkinningConvertInspectionBytes));
    if (conversionStatus != mapping::HandsValidationStatus::valid) {
        result.error =
            L"Skinning-palette conversion structural validation failed: " +
            Widen(mapping::Describe(conversionStatus));
        return result;
    }

    const auto subMeshPattern =
        signature::Parse(mapping::SubMeshGetModelMatrixEntryPattern());
    if (subMeshPattern.status != signature::ParseStatus::valid) {
        result.error = L"Internal cSubMeshEntity::GetModelMatrix signature is malformed.";
        return result;
    }
    const LocatedPattern subMeshGetModelMatrix =
        FindAcrossRegions(regions, subMeshPattern.pattern);
    if (subMeshGetModelMatrix.status == signature::MatchStatus::noMatch) {
        result.error = L"cSubMeshEntity::GetModelMatrix signature was not found.";
        return result;
    }
    if (subMeshGetModelMatrix.status == signature::MatchStatus::ambiguous) {
        result.error = L"cSubMeshEntity::GetModelMatrix signature was ambiguous.";
        return result;
    }
    const auto subMeshAddress =
        reinterpret_cast<std::uintptr_t>(subMeshGetModelMatrix.address);
    const auto subMeshBytes = ExecutableBytesAt(regions, subMeshAddress);
    if (subMeshBytes.size() < mapping::kSubMeshGetModelMatrixInspectionBytes) {
        result.error = L"cSubMeshEntity::GetModelMatrix target is unavailable.";
        return result;
    }
    const mapping::HandsValidationStatus subMeshStatus =
        mapping::ValidateSubMeshGetModelMatrix(subMeshBytes.first(
            mapping::kSubMeshGetModelMatrixInspectionBytes));
    if (subMeshStatus != mapping::HandsValidationStatus::valid) {
        result.error =
            L"cSubMeshEntity::GetModelMatrix structural validation failed: " +
            Widen(mapping::Describe(subMeshStatus));
        return result;
    }

    const auto propUpdatePattern =
        signature::Parse(mapping::LuxPropOnUpdateEntryPattern());
    if (propUpdatePattern.status != signature::ParseStatus::valid) {
        result.error = L"Internal cLuxProp::OnUpdate signature is malformed.";
        return result;
    }
    const LocatedPattern luxPropOnUpdate =
        FindAcrossRegions(regions, propUpdatePattern.pattern);
    if (luxPropOnUpdate.status == signature::MatchStatus::noMatch) {
        result.error = L"cLuxProp::OnUpdate signature was not found.";
        return result;
    }
    if (luxPropOnUpdate.status == signature::MatchStatus::ambiguous) {
        result.error = L"cLuxProp::OnUpdate signature was ambiguous.";
        return result;
    }
    const auto propUpdateAddress =
        reinterpret_cast<std::uintptr_t>(luxPropOnUpdate.address);
    const auto propUpdateBytes =
        ExecutableBytesAt(regions, propUpdateAddress);
    if (propUpdateBytes.size() < mapping::kLuxPropOnUpdateInspectionBytes) {
        result.error = L"cLuxProp::OnUpdate target is unavailable.";
        return result;
    }
    const mapping::WorldValidationStatus propUpdateStatus =
        mapping::ValidateLuxPropOnUpdate(propUpdateBytes.first(
            mapping::kLuxPropOnUpdateInspectionBytes));
    if (propUpdateStatus != mapping::WorldValidationStatus::valid) {
        result.error = L"cLuxProp::OnUpdate structural validation failed: " +
                       Widen(mapping::Describe(propUpdateStatus));
        return result;
    }

    const auto agentUpdatePattern =
        signature::Parse(mapping::AgentOnUpdateEntryPattern());
    if (agentUpdatePattern.status != signature::ParseStatus::valid) {
        result.error = L"Internal cLuxAgent::OnUpdate signature is malformed.";
        return result;
    }
    const LocatedPattern luxAgentOnUpdate =
        FindAcrossRegions(regions, agentUpdatePattern.pattern);
    if (luxAgentOnUpdate.status == signature::MatchStatus::noMatch) {
        result.error = L"cLuxAgent::OnUpdate signature was not found.";
        return result;
    }
    if (luxAgentOnUpdate.status == signature::MatchStatus::ambiguous) {
        result.error = L"cLuxAgent::OnUpdate signature was ambiguous.";
        return result;
    }
    const auto agentUpdateAddress =
        reinterpret_cast<std::uintptr_t>(luxAgentOnUpdate.address);
    const auto agentUpdateBytes =
        ExecutableBytesAt(regions, agentUpdateAddress);
    if (agentUpdateBytes.size() < mapping::kAgentOnUpdateInspectionBytes) {
        result.error = L"cLuxAgent::OnUpdate target is unavailable.";
        return result;
    }
    const mapping::AgentValidationStatus agentUpdateStatus =
        mapping::ValidateAgentOnUpdate(agentUpdateBytes.first(
            mapping::kAgentOnUpdateInspectionBytes));
    if (agentUpdateStatus != mapping::AgentValidationStatus::valid) {
        result.error = L"cLuxAgent::OnUpdate structural validation failed: " +
                       Widen(mapping::Describe(agentUpdateStatus));
        return result;
    }

    const auto guiPixelPattern =
        signature::Parse(mapping::GuiMousePixelSetterEntryPattern());
    if (guiPixelPattern.status != signature::ParseStatus::valid) {
        result.error = L"Internal cImGui pixel mouse-setter signature is malformed.";
        return result;
    }
    const LocatedPattern guiPixelSetter =
        FindAcrossRegions(regions, guiPixelPattern.pattern);
    if (guiPixelSetter.status == signature::MatchStatus::noMatch) {
        result.error = L"cImGui pixel mouse-setter signature was not found.";
        return result;
    }
    if (guiPixelSetter.status == signature::MatchStatus::ambiguous) {
        result.error = L"cImGui pixel mouse-setter signature was ambiguous.";
        return result;
    }
    const auto guiPixelAddress =
        reinterpret_cast<std::uintptr_t>(guiPixelSetter.address);
    const auto guiPixelBytes =
        ExecutableBytesAt(regions, guiPixelAddress);
    if (guiPixelBytes.size() <
        mapping::kGuiMousePixelSetterInspectionBytes) {
        result.error = L"cImGui pixel mouse-setter target is unavailable.";
        return result;
    }
    const auto guiPixelStatus = mapping::ValidateGuiMousePixelSetter(
        guiPixelBytes.first(mapping::kGuiMousePixelSetterInspectionBytes));
    if (guiPixelStatus != mapping::GuiMouseValidationStatus::valid) {
        result.error = L"cImGui pixel mouse-setter validation failed: " +
                       Widen(mapping::Describe(guiPixelStatus));
        return result;
    }

    const auto guiVirtualPattern =
        signature::Parse(mapping::GuiMouseVirtualSetterEntryPattern());
    if (guiVirtualPattern.status != signature::ParseStatus::valid) {
        result.error = L"Internal cImGui virtual mouse-setter signature is malformed.";
        return result;
    }
    const LocatedPattern guiVirtualSetter =
        FindAcrossRegions(regions, guiVirtualPattern.pattern);
    if (guiVirtualSetter.status == signature::MatchStatus::noMatch) {
        result.error = L"cImGui virtual mouse-setter signature was not found.";
        return result;
    }
    if (guiVirtualSetter.status == signature::MatchStatus::ambiguous) {
        result.error = L"cImGui virtual mouse-setter signature was ambiguous.";
        return result;
    }
    const auto guiVirtualAddress =
        reinterpret_cast<std::uintptr_t>(guiVirtualSetter.address);
    const auto guiVirtualBytes =
        ExecutableBytesAt(regions, guiVirtualAddress);
    if (guiVirtualBytes.size() <
        mapping::kGuiMouseVirtualSetterInspectionBytes) {
        result.error = L"cImGui virtual mouse-setter target is unavailable.";
        return result;
    }
    const auto guiVirtualStatus = mapping::ValidateGuiMouseVirtualSetter(
        guiVirtualBytes.first(mapping::kGuiMouseVirtualSetterInspectionBytes));
    if (guiVirtualStatus != mapping::GuiMouseValidationStatus::valid) {
        result.error = L"cImGui virtual mouse-setter validation failed: " +
                       Widen(mapping::Describe(guiVirtualStatus));
        return result;
    }

    const auto guiCursorQueuePattern =
        signature::Parse(mapping::GuiCursorQueueEntryPattern());
    if (guiCursorQueuePattern.status != signature::ParseStatus::valid) {
        result.error = L"Internal cImGui cursor-queue signature is malformed.";
        return result;
    }
    const LocatedPattern guiCursorQueue =
        FindAcrossRegions(regions, guiCursorQueuePattern.pattern);
    if (guiCursorQueue.status == signature::MatchStatus::noMatch) {
        result.error = L"cImGui cursor-queue signature was not found.";
        return result;
    }
    if (guiCursorQueue.status == signature::MatchStatus::ambiguous) {
        result.error = L"cImGui cursor-queue signature was ambiguous.";
        return result;
    }
    const auto guiCursorQueueAddress =
        reinterpret_cast<std::uintptr_t>(guiCursorQueue.address);
    const auto guiCursorQueueBytes =
        ExecutableBytesAt(regions, guiCursorQueueAddress);
    if (guiCursorQueueBytes.size() <
        mapping::kGuiCursorQueueInspectionBytes) {
        result.error = L"cImGui cursor-queue target is unavailable.";
        return result;
    }
    const auto guiCursorQueueStatus = mapping::ValidateGuiCursorQueue(
        guiCursorQueueBytes.first(
            mapping::kGuiCursorQueueInspectionBytes));
    if (guiCursorQueueStatus != mapping::GuiMouseValidationStatus::valid) {
        result.error = L"cImGui cursor-queue validation failed: " +
                       Widen(mapping::Describe(guiCursorQueueStatus));
        return result;
    }

    const auto guiDoMousePattern =
        signature::Parse(mapping::GuiDoMouseEntryPattern());
    if (guiDoMousePattern.status != signature::ParseStatus::valid) {
        result.error = L"Internal cImGui DoMouse signature is malformed.";
        return result;
    }
    const LocatedPattern guiDoMouse =
        FindAcrossRegions(regions, guiDoMousePattern.pattern);
    if (guiDoMouse.status == signature::MatchStatus::noMatch) {
        result.error = L"cImGui DoMouse signature was not found.";
        return result;
    }
    if (guiDoMouse.status == signature::MatchStatus::ambiguous) {
        result.error = L"cImGui DoMouse signature was ambiguous.";
        return result;
    }
    const auto guiDoMouseAddress =
        reinterpret_cast<std::uintptr_t>(guiDoMouse.address);
    const auto guiDoMouseBytes =
        ExecutableBytesAt(regions, guiDoMouseAddress);
    if (guiDoMouseBytes.size() < mapping::kGuiDoMouseInspectionBytes) {
        result.error = L"cImGui DoMouse target is unavailable.";
        return result;
    }
    const auto guiDoMouseStatus = mapping::ValidateGuiDoMouse(
        guiDoMouseBytes.first(mapping::kGuiDoMouseInspectionBytes));
    if (guiDoMouseStatus != mapping::GuiMouseValidationStatus::valid) {
        result.error = L"cImGui DoMouse validation failed: " +
                       Widen(mapping::Describe(guiDoMouseStatus));
        return result;
    }

    result.mappings.engineRun = entryAddress;
    result.mappings.updaterRunMessage = dispatchAddress;
    result.mappings.engineMicroseconds = clockAddress;
    result.mappings.sceneRender = sceneRenderAddress;
    result.mappings.viewportRender = viewportRenderAddress;
    result.mappings.handsSetMatrix = handsAddress;
    result.mappings.attachmentUpdate = attachmentAddress;
    result.mappings.meshUpdateGraphics = meshUpdateAddress;
    result.mappings.skinningConvert = skinningConvertAddress;
    result.mappings.subMeshGetModelMatrix = subMeshAddress;
    result.mappings.luxPropOnUpdate = propUpdateAddress;
    result.mappings.luxAgentOnUpdate = agentUpdateAddress;
    result.mappings.guiMousePixelSetter = guiPixelAddress;
    result.mappings.guiMouseVirtualSetter = guiVirtualAddress;
    result.mappings.guiCursorQueue = guiCursorQueueAddress;
    result.mappings.guiDoMouse = guiDoMouseAddress;
    result.mappings.renderGate = entryAddress + evidence.renderGateOffset;
    result.mappings.flushCall = entryAddress + evidence.flushCallOffset;
    result.mappings.presentCall = entryAddress + evidence.presentCallOffset;
    result.mappings.limitFpsOffset = evidence.limitFpsOffset;
    result.mappings.engineTimerOffset = evidence.engineTimerOffset;
    result.mappings.viewportCameraOffset = viewportEvidence.cameraOffset;
    result.ok = true;
    return result;
}

} // namespace soma::target

