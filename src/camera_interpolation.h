#pragma once

#include "hands_interpolation.h"
#include "presentation_clock.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace soma::camera {

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Pose {
    Vec3 position{};
    float pitch = 0.0F;
    float yaw = 0.0F;
    float roll = 0.0F;
    float extendedPitch = 0.0F;
    float extendedYaw = 0.0F;
    float extendedRoll = 0.0F;
    std::uint32_t projectionType = 0;
    std::uint32_t rotateMode = 0;
    // Authoritative view rotation; meaningful only in matrix rotate mode,
    // which the engine uses for authored and socket-attached cameras.
    hands::Matrix rotation{};
};

inline constexpr std::uint32_t kRotateModeEuler = 0;
inline constexpr std::uint32_t kRotateModeMatrix = 1;

enum class PublishKind : std::uint32_t {
    appended,
    unchanged,
    invalid,
    resetGeneration,
    resetNonMonotonic,
    resetSameTickMutation,
    resetDiscontinuity,
};

enum class SampleKind : std::uint32_t {
    interpolated,
    clockUnavailable,
    generationMismatch,
    snapshotsMissing,
    invalid,
};

struct PublishResult {
    PublishKind kind = PublishKind::invalid;
};

struct SampleResult {
    SampleKind kind = SampleKind::clockUnavailable;
    Pose pose{};
};

class History final {
public:
    static constexpr std::size_t kCapacity =
        clock::PresentationClock::kHistoryCapacity;
    static constexpr float kMaximumPositionDeltaPerTick = 5.0F;
    static constexpr float kMaximumOrientationDeltaPerTick = 2.09439510239F;

    void Reset() noexcept;
    [[nodiscard]] PublishResult Publish(std::uint64_t serial,
                                        std::uint64_t clockGeneration,
                                        const Pose& pose) noexcept;
    [[nodiscard]] SampleResult Evaluate(
        const clock::Sample& clockSample) const noexcept;
    [[nodiscard]] std::size_t SnapshotCount() const noexcept { return count_; }

private:
    struct Snapshot {
        std::uint64_t serial = 0;
        Pose pose{};
    };

    void Append(std::uint64_t serial, const Pose& pose) noexcept;
    [[nodiscard]] const Snapshot& At(std::size_t logicalIndex) const noexcept;

    std::array<Snapshot, kCapacity> snapshots_{};
    std::size_t first_ = 0;
    std::size_t count_ = 0;
    std::uint64_t generation_ = 0;
    bool hasGeneration_ = false;
};

[[nodiscard]] bool IsFinite(const Pose& pose) noexcept;
[[nodiscard]] bool NearlyEqual(const Pose& left, const Pose& right,
                               float epsilon = 1.0e-5F) noexcept;
[[nodiscard]] bool Interpolate(const Pose& older, const Pose& newer,
                               double alpha, Pose& result) noexcept;
// World rotation of an Euler-mode camera: RotY(yaw) * RotX(pitch) *
// RotZ(roll), the inverse of the rotation the engine's view rebuild applies.
[[nodiscard]] hands::Matrix WorldRotation(float pitch, float yaw,
                                          float roll) noexcept;
// Rigid transform that carries anything posed for `interpolated` onto
// `rendered`, turning about the rendered camera position. Used to keep
// first-person content locked to a camera whose rotation was previewed.
[[nodiscard]] bool LookCorrection(const Pose& rendered,
                                  const Pose& interpolated,
                                  hands::Matrix& correction) noexcept;
[[nodiscard]] const wchar_t* PublishKindName(PublishKind kind) noexcept;
[[nodiscard]] const wchar_t* SampleKindName(SampleKind kind) noexcept;

} // namespace soma::camera
