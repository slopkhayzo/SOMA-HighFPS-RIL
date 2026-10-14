#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace soma::mouse {

struct Counts {
    std::int64_t x = 0;
    std::int64_t y = 0;
};

// Learns the radians-per-count relation between delivered mouse counts and
// the authoritative angle change of one axis, and reports whether the mouse
// is currently known to drive it. A direct mouse-look command is exactly
// linear; any smoothing, scripted turn, limit clamp, or special player state
// breaks the relation and disarms the axis. Only a tick in which mouse
// motion demonstrably turned the camera arms it again: idle ticks prove
// nothing, since the mouse may be rotating a held object or a readable.
class AxisModel final {
public:
    static constexpr std::size_t kRatioWindow = 4;
    static constexpr std::int64_t kMinimumLearningCounts = 3;
    static constexpr double kMinimumLearningAngle = 1.0e-5;
    static constexpr double kRatioAgreement = 0.03;
    static constexpr double kRelativeTolerance = 0.05;
    static constexpr double kAbsoluteTolerance = 2.0e-4;

    void Reset() noexcept;
    void Observe(std::int64_t counts, double deltaAngle) noexcept;
    [[nodiscard]] bool Direct() const noexcept {
        return hasScale_ && armed_;
    }
    [[nodiscard]] bool HasScale() const noexcept { return hasScale_; }
    [[nodiscard]] double Scale() const noexcept { return scale_; }
    [[nodiscard]] std::uint64_t Mismatches() const noexcept {
        return mismatches_;
    }

private:
    std::array<double, kRatioWindow> ratios_{};
    std::size_t ratioCount_ = 0;
    std::size_t ratioNext_ = 0;
    double scale_ = 0.0;
    std::uint64_t mismatches_ = 0;
    bool armed_ = false;
    bool hasScale_ = false;
};

struct Offset {
    bool yawValid = false;
    bool pitchValid = false;
    double yaw = 0.0;
    double pitch = 0.0;
};

// Conserves every mouse count while letting presentation observe motion the
// fixed update has not consumed yet. Counts read early are carried and handed
// to the engine's next read, so each count is delivered exactly once.
class LookPreview final {
public:
    // Largest previewed turn; beyond it the pending motion is not trusted.
    static constexpr double kMaximumOffset = 1.0;
    // Calls that pass no time take no part in report-rate smoothing.
    static constexpr double kNoTime = -1.0;
    // Reads further apart than this belong to separate movements.
    static constexpr double kMaximumReportGap = 0.050;
    // A mouse report is never spread over more than this.
    static constexpr double kMaximumSmoothingInterval = 0.020;
    // Smoothing engages only when reports are clearly sparser than reads.
    static constexpr double kSmoothingRatio = 1.5;

    void Reset() noexcept;
    // Presentation read the relative state early. `lateSeconds` says how
    // much later than usual, relative to when the frame will be shown, this
    // read happened; that much of the newest motion is held back so every
    // frame shows the mouse at the same age.
    void OnAcquired(std::int64_t x, std::int64_t y,
                    double timeSeconds = kNoTime,
                    double lateSeconds = 0.0) noexcept;
    // The engine read the relative state; returns what it must receive.
    [[nodiscard]] Counts OnDelivered(std::int64_t x, std::int64_t y,
                                     double timeSeconds = kNoTime) noexcept;
    // The engine flushed the relative state without reading it.
    void OnDiscarded() noexcept;
    // A fixed update completed with this authoritative camera orientation.
    void OnTickBoundary(bool poseValid, double yaw, double pitch) noexcept;
    // Angle not yet represented by the newest authoritative orientation.
    // With a time, a mouse that reports less often than frames are presented
    // has each report spread across one report interval instead of landing
    // in a single frame.
    [[nodiscard]] Offset PendingOffset(
        double timeSeconds = kNoTime) const noexcept;
    [[nodiscard]] double ReportInterval() const noexcept {
        return reportInterval_;
    }
    [[nodiscard]] double ReadInterval() const noexcept {
        return readInterval_;
    }
    [[nodiscard]] bool SmoothingActive() const noexcept;

    [[nodiscard]] Counts Pending() const noexcept { return pending_; }
    [[nodiscard]] Counts Acquired() const noexcept { return acquired_; }
    [[nodiscard]] Counts Delivered() const noexcept { return delivered_; }
    [[nodiscard]] const AxisModel& Yaw(std::size_t lag) const noexcept {
        return yaw_[lag];
    }
    [[nodiscard]] const AxisModel& Pitch(std::size_t lag) const noexcept {
        return pitch_[lag];
    }

private:
    [[nodiscard]] bool AxisOffset(const std::array<AxisModel, 2>& models,
                                  double unapplied,
                                  std::int64_t previousBucket,
                                  double& offset) const noexcept;
    void NoteRead(double timeSeconds) noexcept;
    void NoteMotion(std::int64_t x, std::int64_t y,
                    double timeSeconds) noexcept;
    [[nodiscard]] double WithheldFraction(double timeSeconds) const noexcept;

    Counts pending_{};
    Counts acquired_{};
    Counts delivered_{};
    Counts bucket_{};
    Counts previousBucket_{};
    // Lag 0: counts delivered during a tick move the camera in that tick.
    // Lag 1: they move it one tick later.
    std::array<AxisModel, 2> yaw_{};
    std::array<AxisModel, 2> pitch_{};
    double lastYaw_ = 0.0;
    double lastPitch_ = 0.0;
    bool hasLastPose_ = false;
    // Report-rate smoothing state.
    double lastReadTime_ = 0.0;
    double readInterval_ = 0.0;
    double lastMotionTime_ = 0.0;
    double reportInterval_ = 0.0;
    double lastReportX_ = 0.0;
    double lastReportY_ = 0.0;
    bool hasReadTime_ = false;
    bool hasMotionTime_ = false;
    // Motion since the previous presentation read, and the part of it held
    // back because this read was late.
    Counts sinceRead_{};
    double lateWithheldX_ = 0.0;
    double lateWithheldY_ = 0.0;
};

[[nodiscard]] double WrapAngle(double angle) noexcept;

} // namespace soma::mouse
