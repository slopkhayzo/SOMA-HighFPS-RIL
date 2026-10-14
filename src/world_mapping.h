#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace soma::mapping {

inline constexpr std::size_t kLuxPropOnUpdateInspectionBytes = 0x40;
inline constexpr std::size_t kLuxPropBodiesBeginOffset = 0x930;
inline constexpr std::size_t kLuxPropBodiesEndOffset = 0x938;
inline constexpr std::size_t kLuxPropBodiesCapacityOffset = 0x940;
inline constexpr std::size_t kLuxPropMeshOffset = 0x9f8;
inline constexpr std::size_t kPhysicsBodyIsRagdollOffset = 0x24b;
inline constexpr std::size_t kMaximumLuxPropBodies = 64;

enum class WorldValidationStatus {
    valid,
    windowTooSmall,
    malformedPattern,
    entryMismatch,
};

struct PropBodyVector {
    void* const* begin = nullptr;
    void* const* end = nullptr;
    void* const* capacity = nullptr;
};

[[nodiscard]] std::string_view LuxPropOnUpdateEntryPattern() noexcept;
[[nodiscard]] WorldValidationStatus ValidateLuxPropOnUpdate(
    std::span<const std::uint8_t> code) noexcept;
[[nodiscard]] bool ValidatePropBodyVector(const PropBodyVector& vector,
                                          std::size_t& count) noexcept;
[[nodiscard]] std::string_view Describe(WorldValidationStatus status) noexcept;

} // namespace soma::mapping
