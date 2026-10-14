#include "pose_interpolation.h"

#include <cmath>

namespace soma::pose {
namespace {

bool IsDiscontinuous(const Pose& older, const Pose& newer,
                     const std::uint64_t serialDelta) noexcept {
    const float maximumPosition =
        History::kMaximumPositionDeltaPerTick * static_cast<float>(serialDelta);
    const float maximumPositionSquared = maximumPosition * maximumPosition;
    const float maximumLinearElement = 2.0F * static_cast<float>(serialDelta);
    for (std::size_t index = 0; index < older.boneCount; ++index) {
        const auto& first = older.bones[index].values;
        const auto& second = newer.bones[index].values;
        const float dx = second[3] - first[3];
        const float dy = second[7] - first[7];
        const float dz = second[11] - first[11];
        if (dx * dx + dy * dy + dz * dz > maximumPositionSquared) return true;
        for (std::size_t element = 0; element < 12; ++element) {
            if (element == 3 || element == 7 || element == 11) continue;
            if (std::abs(second[element] - first[element]) >
                maximumLinearElement) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

bool IsValidMatrix(const hands::Matrix& matrix) noexcept {
    for (const float value : matrix.values) {
        if (!std::isfinite(value) || std::abs(value) > 10000.0F) return false;
    }
    constexpr float epsilon = 2.0e-3F;
    if (std::abs(matrix.values[12]) > epsilon ||
        std::abs(matrix.values[13]) > epsilon ||
        std::abs(matrix.values[14]) > epsilon ||
        std::abs(matrix.values[15] - 1.0F) > epsilon) {
        return false;
    }
    const float determinant =
        matrix.values[0] * (matrix.values[5] * matrix.values[10] -
                            matrix.values[6] * matrix.values[9]) -
        matrix.values[1] * (matrix.values[4] * matrix.values[10] -
                            matrix.values[6] * matrix.values[8]) +
        matrix.values[2] * (matrix.values[4] * matrix.values[9] -
                            matrix.values[5] * matrix.values[8]);
    return std::isfinite(determinant) && std::abs(determinant) > 1.0e-5F;
}

bool IsValid(const Pose& pose) noexcept {
    if (pose.boneCount == 0 || pose.boneCount > kMaximumBones) return false;
    for (std::size_t index = 0; index < pose.boneCount; ++index) {
        if (!IsValidMatrix(pose.bones[index])) return false;
    }
    return true;
}

bool NearlyEqual(const Pose& left, const Pose& right) noexcept {
    if (left.boneCount != right.boneCount) return false;
    for (std::size_t index = 0; index < left.boneCount; ++index) {
        if (!hands::NearlyEqual(left.bones[index], right.bones[index])) {
            return false;
        }
    }
    return true;
}

bool Interpolate(const Pose& older, const Pose& newer, const double alpha,
                 Pose& result) noexcept {
    if (!IsValid(older) || !IsValid(newer) ||
        older.boneCount != newer.boneCount || !std::isfinite(alpha) ||
        alpha < 0.0 || alpha > 1.0) {
        return false;
    }
    const float t = static_cast<float>(alpha);
    result = {};
    result.boneCount = older.boneCount;
    for (std::size_t bone = 0; bone < result.boneCount; ++bone) {
        for (std::size_t element = 0; element < 12; ++element) {
            result.bones[bone].values[element] =
                older.bones[bone].values[element] +
                (newer.bones[bone].values[element] -
                 older.bones[bone].values[element]) * t;
        }
        result.bones[bone].values[15] = 1.0F;
    }
    return IsValid(result);
}

void History::Reset() noexcept {
    first_ = 0;
    count_ = 0;
    generation_ = 0;
    hasGeneration_ = false;
}

const History::Snapshot& History::At(
    const std::size_t logicalIndex) const noexcept {
    return snapshots_[(first_ + logicalIndex) % kCapacity];
}

void History::Append(const std::uint64_t serial, const Pose& pose) noexcept {
    if (count_ < kCapacity) {
        snapshots_[(first_ + count_) % kCapacity] = {serial, pose};
        ++count_;
    } else {
        snapshots_[first_] = {serial, pose};
        first_ = (first_ + 1) % kCapacity;
    }
}

PublishKind History::Publish(const std::uint64_t serial,
                             const std::uint64_t clockGeneration,
                             const Pose& pose) noexcept {
    if (!IsValid(pose)) {
        Reset();
        return PublishKind::invalid;
    }
    if (!hasGeneration_ || generation_ != clockGeneration) {
        Reset();
        generation_ = clockGeneration;
        hasGeneration_ = true;
        Append(serial, pose);
        return PublishKind::resetGeneration;
    }
    if (count_ != 0) {
        const Snapshot& latest = At(count_ - 1);
        if (serial < latest.serial) {
            Reset();
            generation_ = clockGeneration;
            hasGeneration_ = true;
            Append(serial, pose);
            return PublishKind::resetNonMonotonic;
        }
        if (serial == latest.serial) {
            if (NearlyEqual(latest.pose, pose)) return PublishKind::unchanged;
            Reset();
            generation_ = clockGeneration;
            hasGeneration_ = true;
            Append(serial, pose);
            return PublishKind::resetSameTickMutation;
        }
        if (latest.pose.boneCount != pose.boneCount) {
            Reset();
            generation_ = clockGeneration;
            hasGeneration_ = true;
            Append(serial, pose);
            return PublishKind::resetBoneCount;
        }
        const std::uint64_t delta = serial - latest.serial;
        if (delta > 1 ||
            IsDiscontinuous(latest.pose, pose, delta)) {
            Reset();
            generation_ = clockGeneration;
            hasGeneration_ = true;
            Append(serial, pose);
            return PublishKind::resetDiscontinuity;
        }
    }
    Append(serial, pose);
    return PublishKind::appended;
}

SampleResult History::Evaluate(
    const clock::Sample& clockSample) const noexcept {
    if (clockSample.kind != clock::SampleKind::interpolated ||
        clockSample.olderSerial == clockSample.newerSerial) {
        return {SampleKind::clockUnavailable, {}};
    }
    if (!hasGeneration_ || generation_ != clockSample.generation) {
        return {SampleKind::generationMismatch, {}};
    }
    const Snapshot* older = nullptr;
    const Snapshot* newer = nullptr;
    for (std::size_t index = 0; index < count_; ++index) {
        const Snapshot& candidate = At(index);
        if (candidate.serial == clockSample.olderSerial) older = &candidate;
        if (candidate.serial == clockSample.newerSerial) newer = &candidate;
    }
    if (older == nullptr || newer == nullptr ||
        older->pose.boneCount != newer->pose.boneCount) {
        return {SampleKind::snapshotsMissing, {}};
    }
    SampleResult result{};
    result.kind = SampleKind::applied;
    if (!Interpolate(older->pose, newer->pose, clockSample.alpha,
                     result.pose)) {
        return {SampleKind::invalid, {}};
    }
    return result;
}

} // namespace soma::pose
