#include "camera_interpolation.h"

#include <algorithm>
#include <cmath>

namespace soma::camera {
namespace {

struct Quaternion {
    float w = 1.0F;
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

Quaternion Multiply(const Quaternion& left, const Quaternion& right) noexcept {
    return {
        left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z,
        left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
    };
}

Quaternion AxisAngle(const float angle, const float x, const float y,
                     const float z) noexcept {
    const float half = angle * 0.5F;
    const float sine = std::sin(half);
    return {std::cos(half), x * sine, y * sine, z * sine};
}

Quaternion FromEuler(const float pitch, const float yaw,
                     const float roll) noexcept {
    return Multiply(Multiply(AxisAngle(yaw, 0.0F, 1.0F, 0.0F),
                             AxisAngle(pitch, 1.0F, 0.0F, 0.0F)),
                    AxisAngle(roll, 0.0F, 0.0F, 1.0F));
}

float Dot(const Quaternion& left, const Quaternion& right) noexcept {
    return left.w * right.w + left.x * right.x + left.y * right.y +
           left.z * right.z;
}

Quaternion Normalize(const Quaternion& value) noexcept {
    const float lengthSquared = Dot(value, value);
    if (!(lengthSquared > 0.0F) || !std::isfinite(lengthSquared)) return {};
    const float inverse = 1.0F / std::sqrt(lengthSquared);
    return {value.w * inverse, value.x * inverse, value.y * inverse,
            value.z * inverse};
}

Quaternion Slerp(const Quaternion& start, const Quaternion& finish,
                 const float alpha) noexcept {
    Quaternion end = finish;
    float cosine = Dot(start, end);
    if (cosine < 0.0F) {
        end = {-end.w, -end.x, -end.y, -end.z};
        cosine = -cosine;
    }
    cosine = std::clamp(cosine, -1.0F, 1.0F);
    if (cosine > 0.9995F) {
        return Normalize({start.w + alpha * (end.w - start.w),
                          start.x + alpha * (end.x - start.x),
                          start.y + alpha * (end.y - start.y),
                          start.z + alpha * (end.z - start.z)});
    }
    const float angle = std::acos(cosine);
    const float sine = std::sin(angle);
    const float startWeight = std::sin((1.0F - alpha) * angle) / sine;
    const float endWeight = std::sin(alpha * angle) / sine;
    return Normalize({start.w * startWeight + end.w * endWeight,
                      start.x * startWeight + end.x * endWeight,
                      start.y * startWeight + end.y * endWeight,
                      start.z * startWeight + end.z * endWeight});
}

void ToEuler(const Quaternion& input, float& pitch, float& yaw,
             float& roll) noexcept {
    const Quaternion q = Normalize(input);
    const float m02 = 2.0F * (q.x * q.z + q.w * q.y);
    const float m10 = 2.0F * (q.x * q.y + q.w * q.z);
    const float m11 = 1.0F - 2.0F * (q.x * q.x + q.z * q.z);
    const float m12 = 2.0F * (q.y * q.z - q.w * q.x);
    const float m22 = 1.0F - 2.0F * (q.x * q.x + q.y * q.y);
    pitch = std::asin(std::clamp(-m12, -1.0F, 1.0F));
    if (std::abs(std::cos(pitch)) > 1.0e-5F) {
        yaw = std::atan2(m02, m22);
        roll = std::atan2(m10, m11);
    } else {
        yaw = std::atan2(-2.0F * (q.x * q.z - q.w * q.y),
                         1.0F - 2.0F * (q.y * q.y + q.z * q.z));
        roll = 0.0F;
    }
}

float OrientationDistance(const float firstPitch, const float firstYaw,
                          const float firstRoll, const float secondPitch,
                          const float secondYaw,
                          const float secondRoll) noexcept {
    const Quaternion first = Normalize(FromEuler(firstPitch, firstYaw, firstRoll));
    const Quaternion second =
        Normalize(FromEuler(secondPitch, secondYaw, secondRoll));
    const float cosine = std::clamp(std::abs(Dot(first, second)), 0.0F, 1.0F);
    return 2.0F * std::acos(cosine);
}

bool IsDiscontinuous(const Pose& older, const Pose& newer) noexcept {
    const float dx = newer.position.x - older.position.x;
    const float dy = newer.position.y - older.position.y;
    const float dz = newer.position.z - older.position.z;
    const float maximumPosition = History::kMaximumPositionDeltaPerTick;
    if (dx * dx + dy * dy + dz * dz > maximumPosition * maximumPosition) {
        return true;
    }
    // Entering or leaving an authored camera is a cut, never a blend.
    if (older.rotateMode != newer.rotateMode ||
        older.projectionType != newer.projectionType) {
        return true;
    }
    if (newer.rotateMode == kRotateModeMatrix) {
        return hands::OrientationDistance(older.rotation, newer.rotation) >
               History::kMaximumOrientationDeltaPerTick;
    }
    if (OrientationDistance(older.pitch, older.yaw, older.roll,
                            newer.pitch, newer.yaw, newer.roll) >
        History::kMaximumOrientationDeltaPerTick) {
        return true;
    }
    return OrientationDistance(
               older.extendedPitch, older.extendedYaw, older.extendedRoll,
               newer.extendedPitch, newer.extendedYaw, newer.extendedRoll) >
           History::kMaximumOrientationDeltaPerTick;
}

} // namespace

bool IsFinite(const Pose& pose) noexcept {
    return std::isfinite(pose.position.x) && std::isfinite(pose.position.y) &&
           std::isfinite(pose.position.z) && std::isfinite(pose.pitch) &&
           std::isfinite(pose.yaw) && std::isfinite(pose.roll) &&
           std::isfinite(pose.extendedPitch) &&
           std::isfinite(pose.extendedYaw) &&
           std::isfinite(pose.extendedRoll) && pose.projectionType <= 1U &&
           (pose.rotateMode == kRotateModeEuler ||
            (pose.rotateMode == kRotateModeMatrix &&
             hands::IsRigid(pose.rotation)));
}

bool NearlyEqual(const Pose& left, const Pose& right,
                 const float epsilon) noexcept {
    return std::abs(left.position.x - right.position.x) <= epsilon &&
           std::abs(left.position.y - right.position.y) <= epsilon &&
           std::abs(left.position.z - right.position.z) <= epsilon &&
           std::abs(left.pitch - right.pitch) <= epsilon &&
           std::abs(left.yaw - right.yaw) <= epsilon &&
           std::abs(left.roll - right.roll) <= epsilon &&
           std::abs(left.extendedPitch - right.extendedPitch) <= epsilon &&
           std::abs(left.extendedYaw - right.extendedYaw) <= epsilon &&
           std::abs(left.extendedRoll - right.extendedRoll) <= epsilon &&
           left.projectionType == right.projectionType &&
           left.rotateMode == right.rotateMode &&
           (left.rotateMode != kRotateModeMatrix ||
            hands::NearlyEqual(left.rotation, right.rotation, epsilon));
}

bool Interpolate(const Pose& older, const Pose& newer, const double alpha,
                 Pose& result) noexcept {
    if (!IsFinite(older) || !IsFinite(newer) ||
        older.projectionType != newer.projectionType ||
        older.rotateMode != newer.rotateMode || !std::isfinite(alpha) ||
        alpha < 0.0 || alpha > 1.0) {
        return false;
    }
    const float t = static_cast<float>(alpha);
    result.position = {
        older.position.x + (newer.position.x - older.position.x) * t,
        older.position.y + (newer.position.y - older.position.y) * t,
        older.position.z + (newer.position.z - older.position.z) * t};
    ToEuler(Slerp(Normalize(FromEuler(older.pitch, older.yaw, older.roll)),
                  Normalize(FromEuler(newer.pitch, newer.yaw, newer.roll)), t),
            result.pitch, result.yaw, result.roll);
    ToEuler(Slerp(Normalize(FromEuler(older.extendedPitch, older.extendedYaw,
                                     older.extendedRoll)),
                  Normalize(FromEuler(newer.extendedPitch, newer.extendedYaw,
                                     newer.extendedRoll)), t),
            result.extendedPitch, result.extendedYaw, result.extendedRoll);
    result.projectionType = older.projectionType;
    result.rotateMode = older.rotateMode;
    result.rotation = {};
    if (older.rotateMode == kRotateModeMatrix) {
        // The matrix alone defines the view here, so the Euler fields keep
        // their authoritative values and are not blended.
        result.pitch = newer.pitch;
        result.yaw = newer.yaw;
        result.roll = newer.roll;
        result.extendedPitch = newer.extendedPitch;
        result.extendedYaw = newer.extendedYaw;
        result.extendedRoll = newer.extendedRoll;
        if (!hands::Interpolate(older.rotation, newer.rotation, alpha,
                                result.rotation)) {
            return false;
        }
    }
    return IsFinite(result);
}

hands::Matrix WorldRotation(const float pitch, const float yaw,
                            const float roll) noexcept {
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    const float cr = std::cos(roll);
    const float sr = std::sin(roll);
    hands::Matrix result = hands::Identity();
    result.values[0] = cy * cr + sy * sp * sr;
    result.values[1] = -cy * sr + sy * sp * cr;
    result.values[2] = sy * cp;
    result.values[4] = cp * sr;
    result.values[5] = cp * cr;
    result.values[6] = -sp;
    result.values[8] = -sy * cr + cy * sp * sr;
    result.values[9] = sy * sr + cy * sp * cr;
    result.values[10] = cy * cp;
    return result;
}

bool LookCorrection(const Pose& rendered, const Pose& interpolated,
                    hands::Matrix& correction) noexcept {
    if (rendered.rotateMode != kRotateModeEuler ||
        interpolated.rotateMode != kRotateModeEuler) {
        return false;
    }
    hands::Matrix inverse{};
    hands::Matrix delta{};
    if (!hands::InverseRigid(WorldRotation(interpolated.pitch,
                                           interpolated.yaw,
                                           interpolated.roll),
                             inverse) ||
        !hands::Multiply(WorldRotation(rendered.pitch, rendered.yaw,
                                       rendered.roll),
                         inverse, delta)) {
        return false;
    }
    // T(c) * delta * T(-c): rotate about the camera position c.
    const std::array<float, 3> c{rendered.position.x, rendered.position.y,
                                 rendered.position.z};
    for (std::size_t row = 0; row < 3; ++row) {
        const float rotated = delta.values[row * 4] * c[0] +
                              delta.values[row * 4 + 1] * c[1] +
                              delta.values[row * 4 + 2] * c[2];
        delta.values[row * 4 + 3] = c[row] - rotated;
    }
    if (!hands::IsRigid(delta)) return false;
    correction = delta;
    return true;
}

void History::Reset() noexcept {
    first_ = 0;
    count_ = 0;
    generation_ = 0;
    hasGeneration_ = false;
}

const History::Snapshot& History::At(const std::size_t logicalIndex) const noexcept {
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

PublishResult History::Publish(const std::uint64_t serial,
                               const std::uint64_t clockGeneration,
                               const Pose& pose) noexcept {
    if (!IsFinite(pose)) {
        Reset();
        return {PublishKind::invalid};
    }
    if (!hasGeneration_ || generation_ != clockGeneration) {
        Reset();
        generation_ = clockGeneration;
        hasGeneration_ = true;
        Append(serial, pose);
        return {PublishKind::resetGeneration};
    }
    if (count_ != 0) {
        const Snapshot& latest = At(count_ - 1);
        if (serial < latest.serial) {
            Reset();
            generation_ = clockGeneration;
            hasGeneration_ = true;
            Append(serial, pose);
            return {PublishKind::resetNonMonotonic};
        }
        if (serial == latest.serial) {
            if (NearlyEqual(latest.pose, pose)) return {PublishKind::unchanged};
            Reset();
            generation_ = clockGeneration;
            hasGeneration_ = true;
            Append(serial, pose);
            return {PublishKind::resetSameTickMutation};
        }
        if (serial - latest.serial != 1 || IsDiscontinuous(latest.pose, pose)) {
            Reset();
            generation_ = clockGeneration;
            hasGeneration_ = true;
            Append(serial, pose);
            return {PublishKind::resetDiscontinuity};
        }
    }
    Append(serial, pose);
    return {PublishKind::appended};
}

SampleResult History::Evaluate(const clock::Sample& sample) const noexcept {
    if (sample.kind != clock::SampleKind::interpolated ||
        sample.olderSerial == sample.newerSerial) {
        return {SampleKind::clockUnavailable, {}};
    }
    if (!hasGeneration_ || generation_ != sample.generation) {
        return {SampleKind::generationMismatch, {}};
    }
    const Snapshot* older = nullptr;
    const Snapshot* newer = nullptr;
    for (std::size_t index = 0; index < count_; ++index) {
        const Snapshot& candidate = At(index);
        if (candidate.serial == sample.olderSerial) older = &candidate;
        if (candidate.serial == sample.newerSerial) newer = &candidate;
    }
    if (older == nullptr || newer == nullptr) {
        return {SampleKind::snapshotsMissing, {}};
    }
    Pose interpolated{};
    if (!Interpolate(older->pose, newer->pose, sample.alpha, interpolated)) {
        return {SampleKind::invalid, {}};
    }
    return {SampleKind::interpolated, interpolated};
}

const wchar_t* PublishKindName(const PublishKind kind) noexcept {
    switch (kind) {
    case PublishKind::appended: return L"appended";
    case PublishKind::unchanged: return L"unchanged";
    case PublishKind::invalid: return L"invalid";
    case PublishKind::resetGeneration: return L"generation";
    case PublishKind::resetNonMonotonic: return L"non-monotonic";
    case PublishKind::resetSameTickMutation: return L"same-tick-mutation";
    case PublishKind::resetDiscontinuity: return L"discontinuity";
    }
    return L"unknown";
}

const wchar_t* SampleKindName(const SampleKind kind) noexcept {
    switch (kind) {
    case SampleKind::interpolated: return L"interpolated";
    case SampleKind::clockUnavailable: return L"clock-unavailable";
    case SampleKind::generationMismatch: return L"generation-mismatch";
    case SampleKind::snapshotsMissing: return L"snapshots-missing";
    case SampleKind::invalid: return L"invalid";
    }
    return L"unknown";
}

} // namespace soma::camera
