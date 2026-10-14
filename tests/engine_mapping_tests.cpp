#include "engine_mapping.h"
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

void WriteRelativeCall(std::vector<std::uint8_t>& code,
                       const std::size_t callOffset,
                       const std::int64_t targetOffset) {
    const std::int64_t displacement =
        targetOffset - static_cast<std::int64_t>(callOffset + 5);
    const auto value = static_cast<std::int32_t>(displacement);
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        code[callOffset + 1 + index] = static_cast<std::uint8_t>(
            (static_cast<std::uint32_t>(value) >> (index * 8)) & 0xffU);
    }
}

std::vector<std::uint8_t> ValidWindow() {
    std::vector<std::uint8_t> code(soma::mapping::kEngineRunInspectionBytes, 0xcc);
    Insert(code, 0x40,
           "48 8B 4E 40 BA 04 00 00 00 0F 28 D0 E8 00 00 00 00 "
           "48 8B 4E 48 E8 00 00 00 00 "
           "48 8B 4E 40 BA 05 00 00 00 0F 28 D0 E8 00 00 00 00");
    WriteRelativeCall(code, 0x40 + 12, -0x300);
    WriteRelativeCall(code, 0x40 + 38, -0x300);
    Insert(code, 0x100,
           "80 3E 00 74 09 40 84 FF 0F 85 01 02 03 04 "
           "80 7E 78 00 74 1F 45 84 F6");
    Insert(code, 0x140,
           "E8 00 00 00 00 48 8B F8 E8 00 00 00 00 "
           "48 8B 4E 70 48 8B 11 8B D8 FF 52 10");
    WriteRelativeCall(code, 0x140, -0x500);
    Insert(code, 0x160,
           "44 8B 46 7C 48 8B 8E C8 00 00 00 F3 0F 10 4E 04 "
           "E8 00 00 00 00");
    WriteRelativeCall(code, 0x160 + 16, 0x700);
    Insert(code, 0x200,
           "4C 8B 9E C0 00 00 00 49 8B 4B 38 48 8B 01 "
           "FF 90 B8 01 00 00");
    Insert(code, 0x300,
           "48 8B 86 C0 00 00 00 48 8B 48 38 48 8B 01 "
           "FF 90 B0 01 00 00");
    return code;
}

} // namespace

int main() {
    using soma::mapping::ValidationStatus;

    auto code = ValidWindow();
    const auto valid = soma::mapping::ValidateEngineRun(code);
    Expect(valid.status == ValidationStatus::valid,
           "complete ordered evidence should validate");
    Expect(valid.limitFpsOffset == 0x78, "LimitFPS offset should be extracted");
    Expect(valid.updateDispatchCallOffset == 0x40 + 12,
           "fixed-update dispatch call offset");
    Expect(valid.schedulerClockCallOffset == 0x140,
           "scheduler clock call offset");
    Expect(valid.sceneRenderCallOffset == 0x160 + 16,
           "scene render call offset");
    Expect(valid.engineTimerOffset == 0x48, "engine timer offset");
    Expect(valid.renderGateOffset == 0x100, "render gate offset");
    Expect(valid.flushCallOffset == 0x200, "flush call offset");
    Expect(valid.presentCallOffset == 0x300, "present call offset");

    auto invalidOffset = code;
    invalidOffset[0x100 + 16] = 0x79;
    Expect(soma::mapping::ValidateEngineRun(invalidOffset).status ==
               ValidationStatus::invalidLimitFpsOffset,
           "misaligned LimitFPS offset should fail closed");

    auto invalidTimerOffset = code;
    invalidTimerOffset[0x40 + 20] = 0x49;
    Expect(soma::mapping::ValidateEngineRun(invalidTimerOffset).status ==
               ValidationStatus::invalidEngineTimerOffset,
           "misaligned engine timer offset should fail closed");

    auto ambiguous = code;
    std::copy_n(code.begin() + 0x100, 23, ambiguous.begin() + 0x400);
    Expect(soma::mapping::ValidateEngineRun(ambiguous).status ==
               ValidationStatus::renderGateAmbiguous,
           "duplicate render gate should fail closed");

    auto mismatchedDispatch = code;
    WriteRelativeCall(mismatchedDispatch, 0x40 + 38, -0x200);
    Expect(soma::mapping::ValidateEngineRun(mismatchedDispatch).status ==
               ValidationStatus::updateDispatchTargetMismatch,
           "fixed-update calls with different targets should fail closed");

    auto missingPresent = code;
    std::fill(missingPresent.begin() + 0x300,
              missingPresent.begin() + 0x300 + 20,
              static_cast<std::uint8_t>(0xcc));
    Expect(soma::mapping::ValidateEngineRun(missingPresent).status ==
               ValidationStatus::presentCallMissing,
           "missing present call should fail closed");

    auto missingClock = code;
    std::fill(missingClock.begin() + 0x140,
              missingClock.begin() + 0x140 + 25,
              static_cast<std::uint8_t>(0xcc));
    Expect(soma::mapping::ValidateEngineRun(missingClock).status ==
               ValidationStatus::schedulerClockMissing,
           "missing scheduler clock call should fail closed");

    auto missingScene = code;
    std::fill(missingScene.begin() + 0x160,
              missingScene.begin() + 0x160 + 21,
              static_cast<std::uint8_t>(0xcc));
    Expect(soma::mapping::ValidateEngineRun(missingScene).status ==
               ValidationStatus::sceneRenderMissing,
           "missing scene render call should fail closed");

    std::vector<std::uint8_t> shortWindow(64, 0xcc);
    Expect(soma::mapping::ValidateEngineRun(shortWindow).status ==
               ValidationStatus::windowTooSmall,
           "short function window should fail closed");

    if (g_failures != 0) {
        std::cerr << g_failures << " engine mapping assertion(s) failed\n";
        return 1;
    }
    std::cout << "engine mapping structural validation tests passed\n";
    return 0;
}
