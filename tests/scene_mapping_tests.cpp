#include "scene_mapping.h"
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

std::vector<std::uint8_t> ValidScene() {
    std::vector<std::uint8_t> code(soma::mapping::kSceneRenderInspectionBytes,
                                   0xcc);
    Insert(code, 0,
           "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 "
           "48 83 EC 50 FF 05 00 00 00 00 33 C0 48 8B F1 "
           "89 05 00 00 00 00 89 05 00 00 00 00 89 05 00 00 00 00 "
           "0F 29 74 24 40 0F 28 F1");
    Insert(code, 0x92,
           "48 8B 53 10 80 7A 29 00 74 0E 0F 28 D6 44 8B CD "
           "48 8B CE E8 00 00 00 00 48 8B 1B 48 3B DF 75 E0");
    return code;
}

std::vector<std::uint8_t> ValidViewport() {
    std::vector<std::uint8_t> code(
        soma::mapping::kViewportRenderInspectionBytes, 0xcc);
    Insert(code, 0,
           "48 89 5C 24 10 44 89 4C 24 20 55 56 57 41 54 41 55 41 56 41 57 "
           "48 83 EC 50 48 8B 42 38 4C 8B 72 30 48 8B FA 0F 29 74 24 40 "
           "0F 28 F2 48 89 84 24 90 00 00 00 48 8B 41 38 4C 8B F9 "
           "4C 8B 60 38 BA 21 00 00 00 41 8B D9 49 8B 04 24 49 8B CC "
           "FF 50 50");
    Insert(code, 0x51,
           "4C 8B 6F 18 85 C0 0F 95 C2 4D 85 ED 74 0D "
           "49 8B CD E8 00 00 00 00");
    return code;
}

} // namespace

int main() {
    using soma::mapping::SceneValidationStatus;
    using soma::mapping::ViewportValidationStatus;

    auto scene = ValidScene();
    const auto sceneValid = soma::mapping::ValidateSceneRender(scene);
    Expect(sceneValid.status == SceneValidationStatus::valid,
           "scene structure should validate");
    Expect(sceneValid.viewportRenderCallOffset == 0xa5,
           "viewport dispatch call offset");

    auto duplicate = scene;
    std::copy_n(scene.begin() + 0x92, 32, duplicate.begin() + 0xc0);
    Expect(soma::mapping::ValidateSceneRender(duplicate).status ==
               SceneValidationStatus::viewportDispatchAmbiguous,
           "duplicate viewport dispatch should fail closed");

    auto viewport = ValidViewport();
    const auto viewportValid = soma::mapping::ValidateViewportRender(viewport);
    Expect(viewportValid.status == ViewportValidationStatus::valid,
           "viewport structure should validate");
    Expect(viewportValid.cameraOffset == 0x18,
           "camera offset should be extracted");

    viewport[0x51 + 3] = 0x19;
    Expect(soma::mapping::ValidateViewportRender(viewport).status ==
               ViewportValidationStatus::invalidCameraOffset,
           "misaligned camera offset should fail closed");

    if (g_failures != 0) {
        std::cerr << g_failures << " scene mapping assertion(s) failed\n";
        return 1;
    }
    std::cout << "scene and viewport mapping tests passed\n";
    return 0;
}
