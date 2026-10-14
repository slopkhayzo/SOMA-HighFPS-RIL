#pragma once

#include "hands_interpolation.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace soma::world {

inline constexpr std::uint64_t kMaximumRecentSerialAge = 2;
// Every submesh keeps its own final-model history, so a prop is eligible at
// any body count the prop layout validator accepts.
inline constexpr std::uint32_t kMaximumBodyCount = 64;
// Skeleton-physics props above this size stay stock until ragdoll palettes
// are interpolated together with their roots.
inline constexpr std::uint32_t kMaximumRagdollRootBodyCount = 2;

enum class Eligibility {
    eligible,
    disabled,
    wrongBodyCount,
    ragdoll,
    stale,
    clockUnavailable,
};

[[nodiscard]] Eligibility ClassifyEligibility(
    bool enabled, std::uint32_t bodyCount, std::uint32_t ragdollBodyCount,
    bool scriptDriven, std::uint64_t currentSerial,
    std::uint64_t lastSeenSerial, bool clockAvailable) noexcept;

class ModelTrack {
public:
    static constexpr std::size_t kCapacity =
        clock::PresentationClock::kHistoryCapacity;

    void Reset() noexcept;
    [[nodiscard]] hands::PublishKind Capture(
        std::uintptr_t owner, std::uint64_t serial,
        std::uint64_t clockGeneration,
        const hands::Matrix& authoritative) noexcept;
    [[nodiscard]] hands::SampleResult Evaluate(
        const clock::Sample& sample) const noexcept;
    [[nodiscard]] std::uintptr_t Owner() const noexcept { return owner_; }

private:
    struct Snapshot {
        std::uint64_t serial = 0;
        hands::Matrix matrix{};
    };

    void ResetHistory() noexcept;
    void Append(std::uint64_t serial,
                const hands::Matrix& matrix) noexcept;
    [[nodiscard]] const Snapshot& At(std::size_t logicalIndex) const noexcept;

    std::array<Snapshot, kCapacity> snapshots_{};
    std::size_t first_ = 0;
    std::size_t count_ = 0;
    std::uint64_t generation_ = 0;
    bool hasGeneration_ = false;
    std::uintptr_t owner_ = 0;
};

// World submesh matrices may contain a fixed local offset, tiny uniform scale,
// or non-uniform asset scale. Interpolate the rigid delta between endpoints
// and compose it with the older affine endpoint so those local terms survive.
[[nodiscard]] bool IsUsableAffine(const hands::Matrix& matrix) noexcept;
[[nodiscard]] bool InterpolateAffineMotion(
    const hands::Matrix& older, const hands::Matrix& newer, double alpha,
    hands::Matrix& result) noexcept;

} // namespace soma::world
