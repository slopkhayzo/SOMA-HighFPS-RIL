#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace soma::mapping {

inline constexpr std::size_t kAgentOnUpdateInspectionBytes = 0x40;

enum class AgentValidationStatus {
    valid,
    windowTooSmall,
    malformedPattern,
    entryMismatch,
};

[[nodiscard]] std::string_view AgentOnUpdateEntryPattern() noexcept;
[[nodiscard]] AgentValidationStatus ValidateAgentOnUpdate(
    std::span<const std::uint8_t> code) noexcept;
[[nodiscard]] std::string_view Describe(AgentValidationStatus status) noexcept;

} // namespace soma::mapping
