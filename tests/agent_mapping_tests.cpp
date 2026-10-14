#include "agent_mapping.h"

#include "signature_scan.h"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

int g_failures = 0;

void Expect(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++g_failures;
}

void Insert(std::vector<std::uint8_t>& destination,
            const std::string_view patternText) {
    const auto parsed = soma::signature::Parse(patternText);
    Expect(parsed.status == soma::signature::ParseStatus::valid,
           "agent signature should parse");
    if (parsed.status != soma::signature::ParseStatus::valid) return;
    for (std::size_t index = 0; index < parsed.pattern.bytes.size(); ++index) {
        destination[index] = parsed.pattern.bytes[index].wildcard
            ? static_cast<std::uint8_t>(0x31U + index)
            : parsed.pattern.bytes[index].value;
    }
}

} // namespace

int main() {
    using soma::mapping::AgentValidationStatus;
    std::vector<std::uint8_t> code(
        soma::mapping::kAgentOnUpdateInspectionBytes, 0xcc);
    Insert(code, soma::mapping::AgentOnUpdateEntryPattern());
    Expect(soma::mapping::ValidateAgentOnUpdate(code) ==
               AgentValidationStatus::valid,
           "exact cLuxAgent::OnUpdate entry should validate");
    code[10] = 0x35;
    Expect(soma::mapping::ValidateAgentOnUpdate(code) ==
               AgentValidationStatus::entryMismatch,
           "different agent state field should fail closed");

    std::vector<std::uint8_t> shortCode(32, 0xcc);
    Expect(soma::mapping::ValidateAgentOnUpdate(shortCode) ==
               AgentValidationStatus::windowTooSmall,
           "short agent update window should fail closed");

    if (g_failures != 0) return 1;
    std::cout << "agent mapping tests passed\n";
    return 0;
}
