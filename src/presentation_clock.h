#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace soma::clock {

enum class ResetReason : std::uint32_t {
    none,
    explicitDiscontinuity,
    invalidConfiguration,
    nonMonotonicWallTime,
    nonMonotonicTick,
    missingSnapshots,
    longStall,
};

enum class SampleKind : std::uint32_t {
    unavailable,
    interpolated,
    reset,
};

struct Configuration {
    double fixedStepSeconds = 0.0;
    double longStallSeconds = 0.250;
    std::uint64_t maximumSerialDelta = 1;
};

struct PublishResult {
    bool accepted = false;
    ResetReason resetReason = ResetReason::none;
};

struct Sample {
    SampleKind kind = SampleKind::unavailable;
    ResetReason resetReason = ResetReason::none;
    std::uint64_t generation = 0;
    std::uint64_t olderSerial = 0;
    std::uint64_t newerSerial = 0;
    double alpha = 0.0;
    double targetTime = 0.0;
};

class PresentationClock final {
public:
    static constexpr std::size_t kHistoryCapacity = 2;

    explicit PresentationClock(Configuration configuration) noexcept;

    [[nodiscard]] bool IsConfigured() const noexcept { return configured_; }
    [[nodiscard]] const Configuration& Config() const noexcept { return configuration_; }
    [[nodiscard]] PublishResult Publish(std::uint64_t serial, double wallTime,
                                        bool discontinuity = false) noexcept;
    [[nodiscard]] Sample Evaluate(double wallTime, double interpolationAlpha,
                                  bool phaseValid = true) noexcept;
    void Reset(ResetReason reason = ResetReason::explicitDiscontinuity) noexcept;

    [[nodiscard]] std::size_t SnapshotCount() const noexcept { return count_; }
    [[nodiscard]] std::uint64_t Generation() const noexcept { return generation_; }
    [[nodiscard]] ResetReason LastResetReason() const noexcept { return lastResetReason_; }

private:
    struct Snapshot {
        std::uint64_t serial = 0;
        double wallArrivalTime = 0.0;
    };

    void Clear(ResetReason reason) noexcept;
    void Append(std::uint64_t serial, double wallTime) noexcept;
    [[nodiscard]] const Snapshot& At(std::size_t logicalIndex) const noexcept;

    Configuration configuration_{};
    bool configured_ = false;
    std::array<Snapshot, kHistoryCapacity> snapshots_{};
    std::size_t first_ = 0;
    std::size_t count_ = 0;
    std::uint64_t generation_ = 0;
    ResetReason lastResetReason_ = ResetReason::none;
    double lastSampleWallTime_ = 0.0;
    bool hasLastSample_ = false;
};

[[nodiscard]] bool LogicTimerAlpha(double nextTickMilliseconds,
                                   double stepMilliseconds,
                                   double speedMultiplier,
                                   double nowMilliseconds,
                                   double presentationDelayTicks,
                                   double& alpha,
                                   double* rawAlpha = nullptr) noexcept;
[[nodiscard]] const wchar_t* ResetReasonName(ResetReason reason) noexcept;
[[nodiscard]] const wchar_t* SampleKindName(SampleKind kind) noexcept;

} // namespace soma::clock

