#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>

namespace soma::target {

struct HostIdentity {
    std::uint16_t machine = 0;
    std::uint32_t timestamp = 0;
    std::uint32_t imageSize = 0;
    std::uint32_t codeSize = 0;
    std::uintptr_t imageBase = 0;
};

struct InspectionResult {
    bool ok = false;
    HostIdentity identity{};
    std::wstring error;
};

struct RuntimeMappings {
    std::uintptr_t engineRun = 0;
    std::uintptr_t updaterRunMessage = 0;
    std::uintptr_t engineMicroseconds = 0;
    std::uintptr_t sceneRender = 0;
    std::uintptr_t viewportRender = 0;
    std::uintptr_t handsSetMatrix = 0;
    std::uintptr_t attachmentUpdate = 0;
    std::uintptr_t meshUpdateGraphics = 0;
    std::uintptr_t skinningConvert = 0;
    std::uintptr_t subMeshGetModelMatrix = 0;
    std::uintptr_t luxPropOnUpdate = 0;
    std::uintptr_t luxAgentOnUpdate = 0;
    std::uintptr_t guiMousePixelSetter = 0;
    std::uintptr_t guiMouseVirtualSetter = 0;
    std::uintptr_t guiCursorQueue = 0;
    std::uintptr_t guiDoMouse = 0;
    std::uintptr_t renderGate = 0;
    std::uintptr_t flushCall = 0;
    std::uintptr_t presentCall = 0;
    std::uint8_t limitFpsOffset = 0;
    std::uint8_t engineTimerOffset = 0;
    std::uint8_t viewportCameraOffset = 0;
};

struct MappingResult {
    bool ok = false;
    RuntimeMappings mappings{};
    std::wstring error;
};

[[nodiscard]] InspectionResult InspectMappedHost(HMODULE executable) noexcept;
[[nodiscard]] MappingResult ResolveRuntimeMappings(HMODULE executable) noexcept;

} // namespace soma::target

