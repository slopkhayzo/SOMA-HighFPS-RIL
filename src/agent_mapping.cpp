#include "agent_mapping.h"

#include "signature_scan.h"

namespace soma::mapping {
namespace {

// cLuxAgent::OnUpdate(float). The method runs native agent update helpers,
// invokes the script OnUpdate callback, then completes native post-update work.
constexpr std::string_view kAgentOnUpdateEntry =
    "40 53 48 83 EC 60 F3 0F 10 81 34 09 00 00 "
    "0F 29 74 24 50 48 8B D9 0F 2F 05 ?? ?? ?? ?? "
    "0F 28 F1 76 0C F3 0F 5C C6 F3 0F 11 81 34 09 00 00 "
    "E8 ?? ?? ?? ?? 0F 28 CE 48 8B CB E8 ?? ?? ?? ?? 48 8B";

} // namespace

std::string_view AgentOnUpdateEntryPattern() noexcept {
    return kAgentOnUpdateEntry;
}

AgentValidationStatus ValidateAgentOnUpdate(
    const std::span<const std::uint8_t> code) noexcept {
    if (code.size() < kAgentOnUpdateInspectionBytes) {
        return AgentValidationStatus::windowTooSmall;
    }
    const auto pattern = signature::Parse(kAgentOnUpdateEntry);
    if (pattern.status != signature::ParseStatus::valid) {
        return AgentValidationStatus::malformedPattern;
    }
    const auto match = signature::FindUnique(
        code.first(kAgentOnUpdateInspectionBytes), pattern.pattern);
    if (match.status != signature::MatchStatus::unique || match.offset != 0) {
        return AgentValidationStatus::entryMismatch;
    }
    return AgentValidationStatus::valid;
}

std::string_view Describe(const AgentValidationStatus status) noexcept {
    switch (status) {
    case AgentValidationStatus::valid: return "valid";
    case AgentValidationStatus::windowTooSmall:
        return "inspection window is too small";
    case AgentValidationStatus::malformedPattern:
        return "internal pattern is malformed";
    case AgentValidationStatus::entryMismatch:
        return "cLuxAgent::OnUpdate entry does not match";
    }
    return "unknown agent mapping validation status";
}

} // namespace soma::mapping
