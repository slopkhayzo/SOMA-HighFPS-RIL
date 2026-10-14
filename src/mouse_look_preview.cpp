#include "mouse_look_preview.h"

#include <algorithm>
#include <cmath>

namespace soma::mouse {
namespace {

constexpr double kPi = 3.14159265358979323846;

} // namespace

double WrapAngle(const double angle) noexcept {
    if (!std::isfinite(angle)) return 0.0;
    double wrapped = std::fmod(angle + kPi, 2.0 * kPi);
    if (wrapped < 0.0) wrapped += 2.0 * kPi;
    return wrapped - kPi;
}

void AxisModel::Reset() noexcept {
    ratios_ = {};
    ratioCount_ = 0;
    ratioNext_ = 0;
    scale_ = 0.0;
    mismatches_ = 0;
    armed_ = false;
    hasScale_ = false;
}

void AxisModel::Observe(const std::int64_t counts,
                        const double deltaAngle) noexcept {
    if (!std::isfinite(deltaAngle)) {
        Reset();
        return;
    }
    const double magnitude = static_cast<double>(counts < 0 ? -counts : counts);
    if (hasScale_) {
        const double predicted = scale_ * static_cast<double>(counts);
        const double tolerance = (std::max)(
            kAbsoluteTolerance, kRelativeTolerance * std::abs(predicted));
        if (std::abs(deltaAngle - predicted) > tolerance) {
            armed_ = false;
            ++mismatches_;
        } else if (counts != 0 && std::abs(predicted) > kAbsoluteTolerance) {
            armed_ = true;
        }
    }

    if (magnitude < static_cast<double>(kMinimumLearningCounts)) return;
    if (std::abs(deltaAngle) < kMinimumLearningAngle) {
        // Motion that turned nothing (a limit, a menu, a locked state) is
        // evidence against any pending estimate.
        ratioCount_ = 0;
        ratioNext_ = 0;
        return;
    }
    ratios_[ratioNext_] = deltaAngle / static_cast<double>(counts);
    ratioNext_ = (ratioNext_ + 1) % kRatioWindow;
    if (ratioCount_ < kRatioWindow) ++ratioCount_;
    if (ratioCount_ < kRatioWindow) return;

    double mean = 0.0;
    for (const double ratio : ratios_) mean += ratio;
    mean /= static_cast<double>(kRatioWindow);
    for (const double ratio : ratios_) {
        if (std::abs(ratio - mean) > kRatioAgreement * std::abs(mean)) return;
    }
    if (!hasScale_ ||
        std::abs(mean - scale_) > kRatioAgreement * std::abs(scale_)) {
        scale_ = mean;
        hasScale_ = true;
        armed_ = true;
    }
}

void LookPreview::Reset() noexcept {
    // Pending counts are owed to the engine and survive a model reset.
    acquired_ = {};
    delivered_ = {};
    bucket_ = {};
    previousBucket_ = {};
    for (auto& model : yaw_) model.Reset();
    for (auto& model : pitch_) model.Reset();
    lastYaw_ = 0.0;
    lastPitch_ = 0.0;
    hasLastPose_ = false;
    lastReadTime_ = 0.0;
    readInterval_ = 0.0;
    lastMotionTime_ = 0.0;
    reportInterval_ = 0.0;
    lastReportX_ = 0.0;
    lastReportY_ = 0.0;
    hasReadTime_ = false;
    hasMotionTime_ = false;
    sinceRead_ = {};
    lateWithheldX_ = 0.0;
    lateWithheldY_ = 0.0;
}

void LookPreview::NoteRead(const double timeSeconds) noexcept {
    if (hasReadTime_) {
        const double elapsed = timeSeconds - lastReadTime_;
        if (elapsed > 0.0 && elapsed <= kMaximumReportGap) {
            readInterval_ = readInterval_ == 0.0
                ? elapsed
                : readInterval_ + 0.1 * (elapsed - readInterval_);
        }
    }
    lastReadTime_ = timeSeconds;
    hasReadTime_ = true;
}

void LookPreview::NoteMotion(const std::int64_t x, const std::int64_t y,
                             const double timeSeconds) noexcept {
    if (x == 0 && y == 0) return;
    // Whatever part of the previous report is still withheld joins this one,
    // so nothing shown so far is taken back.
    const double carried = WithheldFraction(timeSeconds);
    if (hasMotionTime_) {
        const double elapsed = timeSeconds - lastMotionTime_;
        if (elapsed > 0.0 && elapsed <= kMaximumReportGap) {
            // Reads quantize report arrival to frame times, so only the
            // average gap recovers the device period.
            reportInterval_ = reportInterval_ == 0.0
                ? elapsed
                : reportInterval_ + 0.1 * (elapsed - reportInterval_);
        }
    }
    lastReportX_ = lastReportX_ * carried + static_cast<double>(x);
    lastReportY_ = lastReportY_ * carried + static_cast<double>(y);
    lastMotionTime_ = timeSeconds;
    hasMotionTime_ = true;
}

bool LookPreview::SmoothingActive() const noexcept {
    return readInterval_ > 0.0 &&
           reportInterval_ > kSmoothingRatio * readInterval_;
}

double LookPreview::WithheldFraction(const double timeSeconds) const noexcept {
    if (timeSeconds < 0.0 || !hasMotionTime_ || !SmoothingActive()) {
        return 0.0;
    }
    const double interval =
        (std::min)(reportInterval_, kMaximumSmoothingInterval);
    const double elapsed = timeSeconds - lastMotionTime_;
    if (elapsed < 0.0) return 0.0;
    return 1.0 - (std::min)(elapsed / interval, 1.0);
}

void LookPreview::OnAcquired(const std::int64_t x, const std::int64_t y,
                             const double timeSeconds,
                             const double lateSeconds) noexcept {
    pending_.x += x;
    pending_.y += y;
    acquired_.x += x;
    acquired_.y += y;
    sinceRead_.x += x;
    sinceRead_.y += y;
    lateWithheldX_ = 0.0;
    lateWithheldY_ = 0.0;
    if (timeSeconds >= 0.0) {
        const double elapsed = timeSeconds - lastReadTime_;
        if (hasReadTime_ && lateSeconds > 0.0 && elapsed > 0.0 &&
            elapsed <= kMaximumReportGap) {
            // Treat motion as uniform across the read gap and hold back the
            // share that arrived during the late part of it.
            const double fraction = (std::min)(lateSeconds / elapsed, 1.0);
            lateWithheldX_ = static_cast<double>(sinceRead_.x) * fraction;
            lateWithheldY_ = static_cast<double>(sinceRead_.y) * fraction;
        }
        NoteRead(timeSeconds);
        NoteMotion(x, y, timeSeconds);
    }
    sinceRead_ = {};
}

Counts LookPreview::OnDelivered(const std::int64_t x, const std::int64_t y,
                                const double timeSeconds) noexcept {
    // Only the engine's own share is new motion; the rest was noted when
    // presentation read it.
    if (timeSeconds >= 0.0) NoteMotion(x, y, timeSeconds);
    sinceRead_.x += x;
    sinceRead_.y += y;
    const Counts total{x + pending_.x, y + pending_.y};
    pending_ = {};
    bucket_.x += total.x;
    bucket_.y += total.y;
    delivered_.x += total.x;
    delivered_.y += total.y;
    return total;
}

void LookPreview::OnDiscarded() noexcept {
    pending_ = {};
}

void LookPreview::OnTickBoundary(const bool poseValid, const double yaw,
                                 const double pitch) noexcept {
    if (!poseValid || !std::isfinite(yaw) || !std::isfinite(pitch)) {
        for (auto& model : yaw_) model.Reset();
        for (auto& model : pitch_) model.Reset();
        hasLastPose_ = false;
        previousBucket_ = {};
        bucket_ = {};
        return;
    }
    if (hasLastPose_) {
        const double deltaYaw = WrapAngle(yaw - lastYaw_);
        const double deltaPitch = WrapAngle(pitch - lastPitch_);
        yaw_[0].Observe(bucket_.x, deltaYaw);
        yaw_[1].Observe(previousBucket_.x, deltaYaw);
        pitch_[0].Observe(bucket_.y, deltaPitch);
        pitch_[1].Observe(previousBucket_.y, deltaPitch);
    }
    lastYaw_ = yaw;
    lastPitch_ = pitch;
    hasLastPose_ = true;
    previousBucket_ = bucket_;
    bucket_ = {};
}

bool LookPreview::AxisOffset(const std::array<AxisModel, 2>& models,
                             const double unapplied,
                             const std::int64_t previousBucket,
                             double& offset) const noexcept {
    offset = 0.0;
    if (models[0].Direct()) {
        offset = models[0].Scale() * unapplied;
    } else if (models[1].Direct()) {
        offset = models[1].Scale() *
                 (unapplied + static_cast<double>(previousBucket));
    } else {
        return false;
    }
    if (!std::isfinite(offset) || std::abs(offset) > kMaximumOffset) {
        offset = 0.0;
        return false;
    }
    return true;
}

Offset LookPreview::PendingOffset(const double timeSeconds) const noexcept {
    Offset result{};
    if (!hasLastPose_) return result;
    // Counts read early plus any already handed over since the last
    // completed tick are not yet visible in the authoritative orientation.
    // The withheld part of the newest report is subtracted even after the
    // engine has consumed it, which keeps the view continuous across a tick.
    const double withheld = WithheldFraction(timeSeconds);
    result.yawValid = AxisOffset(
        yaw_,
        static_cast<double>(pending_.x + bucket_.x) -
            lastReportX_ * withheld - lateWithheldX_,
        previousBucket_.x, result.yaw);
    result.pitchValid = AxisOffset(
        pitch_,
        static_cast<double>(pending_.y + bucket_.y) -
            lastReportY_ * withheld - lateWithheldY_,
        previousBucket_.y, result.pitch);
    return result;
}

} // namespace soma::mouse
