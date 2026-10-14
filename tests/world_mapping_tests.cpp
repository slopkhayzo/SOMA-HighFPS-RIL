#include "signature_scan.h"
#include "world_mapping.h"

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
    using soma::mapping::PropBodyVector;
    using soma::mapping::WorldValidationStatus;
    static_assert(soma::mapping::kLuxPropBodiesBeginOffset == 0x930);
    static_assert(soma::mapping::kLuxPropMeshOffset == 0x9f8);
    static_assert(soma::mapping::kPhysicsBodyIsRagdollOffset == 0x24b);

    std::vector<std::uint8_t> code(
        soma::mapping::kLuxPropOnUpdateInspectionBytes, 0xcc);
    Insert(code, 0, soma::mapping::LuxPropOnUpdateEntryPattern());
    Expect(soma::mapping::ValidateLuxPropOnUpdate(code) ==
               WorldValidationStatus::valid,
           "exact cLuxProp update entry should validate");
    code[36] = 0x71;
    Expect(soma::mapping::ValidateLuxPropOnUpdate(code) ==
               WorldValidationStatus::entryMismatch,
           "different active-state field should fail closed");
    Expect(soma::mapping::ValidateLuxPropOnUpdate(
               std::span<const std::uint8_t>(code).first(32)) ==
               WorldValidationStatus::windowTooSmall,
           "short prop-update window should fail closed");

    alignas(void*) std::uint8_t storage[16 * sizeof(void*)]{};
    std::size_t count = 0;
    Expect(soma::mapping::ValidatePropBodyVector(
               PropBodyVector{reinterpret_cast<void* const*>(storage),
                              reinterpret_cast<void* const*>(storage) + 1,
                              reinterpret_cast<void* const*>(storage) + 4},
               count) && count == 1,
           "one-body vector should validate");
    Expect(!soma::mapping::ValidatePropBodyVector(
               PropBodyVector{reinterpret_cast<void* const*>(storage) + 2,
                              reinterpret_cast<void* const*>(storage) + 1,
                              reinterpret_cast<void* const*>(storage) + 4},
               count),
           "reversed vector should fail closed");
    Expect(!soma::mapping::ValidatePropBodyVector(
               PropBodyVector{reinterpret_cast<void* const*>(storage),
                              reinterpret_cast<void* const*>(storage) + 1,
                              reinterpret_cast<void* const*>(storage) + 65},
               count),
           "oversized capacity should fail closed");

    if (g_failures != 0) return 1;
    std::cout << "world mapping tests passed\n";
    return 0;
}
