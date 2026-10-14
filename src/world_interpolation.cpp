#include "world_interpolation.h"

#include <algorithm>
#include <cmath>

namespace soma::world {
namespace {

constexpr float kAffineFooterEpsilon = 2.0e-3F;

bool InverseAffine(const hands::Matrix& matrix,
                   hands::Matrix& result) noexcept {
    if (!IsUsableAffine(matrix)) return false;
    const auto& m = matrix.values;
    const float determinant =
        m[0] * (m[5] * m[10] - m[6] * m[9]) -
        m[1] * (m[4] * m[10] - m[6] * m[8]) +
        m[2] * (m[4] * m[9] - m[5] * m[8]);
    const float inverseDeterminant = 1.0F / determinant;

    result = hands::Identity();
    auto& r = result.values;
    r[0] = (m[5] * m[10] - m[6] * m[9]) * inverseDeterminant;
    r[1] = (m[2] * m[9] - m[1] * m[10]) * inverseDeterminant;
    r[2] = (m[1] * m[6] - m[2] * m[5]) * inverseDeterminant;
    r[4] = (m[6] * m[8] - m[4] * m[10]) * inverseDeterminant;
    r[5] = (m[0] * m[10] - m[2] * m[8]) * inverseDeterminant;
    r[6] = (m[2] * m[4] - m[0] * m[6]) * inverseDeterminant;
    r[8] = (m[4] * m[9] - m[5] * m[8]) * inverseDeterminant;
    r[9] = (m[1] * m[8] - m[0] * m[9]) * inverseDeterminant;
    r[10] = (m[0] * m[5] - m[1] * m[4]) * inverseDeterminant;
    r[3] = -(r[0] * m[3] + r[1] * m[7] + r[2] * m[11]);
    r[7] = -(r[4] * m[3] + r[5] * m[7] + r[6] * m[11]);
    r[11] = -(r[8] * m[3] + r[9] * m[7] + r[10] * m[11]);
    return IsUsableAffine(result);
}

bool RigidDelta(const hands::Matrix& older, const hands::Matrix& newer,
                hands::Matrix& delta) noexcept {
    hands::Matrix inverse{};
    return InverseAffine(older, inverse) &&
           hands::Multiply(newer, inverse, delta) && hands::IsRigid(delta);
}

bool IsDiscontinuous(const hands::Matrix& older,
                     const hands::Matrix& newer,
                     const std::uint64_t serialDelta) noexcept {
    const float dx = newer.values[3] - older.values[3];
    const float dy = newer.values[7] - older.values[7];
    const float dz = newer.values[11] - older.values[11];
    const float allowedPosition =
        hands::History::kMaximumPositionDeltaPerTick *
        static_cast<float>(serialDelta);
    if (dx * dx + dy * dy + dz * dz >
        allowedPosition * allowedPosition) {
        return true;
    }
    hands::Matrix delta{};
    if (!RigidDelta(older, newer, delta)) return true;
    const float allowedAngle =
        hands::History::kMaximumOrientationDeltaPerTick *
        static_cast<float>(serialDelta);
    return hands::OrientationDistance(hands::Identity(), delta) >
           allowedAngle;
}

} // namespace

Eligibility ClassifyEligibility(
    const bool enabled, const std::uint32_t bodyCount,
    const std::uint32_t ragdollBodyCount, const bool scriptDriven,
    const std::uint64_t currentSerial, const std::uint64_t lastSeenSerial,
    const bool clockAvailable) noexcept {
    if (!enabled) return Eligibility::disabled;
    // A prop without physics bodies only moves when a script or attachment
    // sets its matrix, so it qualifies once that has been observed.
    if (bodyCount > kMaximumBodyCount || (bodyCount == 0 && !scriptDriven)) {
        return Eligibility::wrongBodyCount;
    }
    if (ragdollBodyCount != 0 && bodyCount > kMaximumRagdollRootBodyCount) {
        return Eligibility::ragdoll;
    }
    if (!clockAvailable) return Eligibility::clockUnavailable;
    if (lastSeenSerial > currentSerial ||
        currentSerial - lastSeenSerial > kMaximumRecentSerialAge) {
        return Eligibility::stale;
    }
    return Eligibility::eligible;
}

void ModelTrack::Reset() noexcept {
    ResetHistory();
    owner_ = 0;
}

void ModelTrack::ResetHistory() noexcept {
    first_ = 0;
    count_ = 0;
    generation_ = 0;
    hasGeneration_ = false;
}

const ModelTrack::Snapshot& ModelTrack::At(
    const std::size_t logicalIndex) const noexcept {
    return snapshots_[(first_ + logicalIndex) % kCapacity];
}

void ModelTrack::Append(const std::uint64_t serial,
                        const hands::Matrix& matrix) noexcept {
    if (count_ < kCapacity) {
        snapshots_[(first_ + count_) % kCapacity] = {serial, matrix};
        ++count_;
    } else {
        snapshots_[first_] = {serial, matrix};
        first_ = (first_ + 1) % kCapacity;
    }
}

hands::PublishKind ModelTrack::Capture(
    const std::uintptr_t owner, const std::uint64_t serial,
    const std::uint64_t clockGeneration,
    const hands::Matrix& authoritative) noexcept {
    if (owner == 0 || !IsUsableAffine(authoritative)) {
        Reset();
        return hands::PublishKind::invalid;
    }
    if (owner_ != owner) {
        ResetHistory();
        owner_ = owner;
    }
    if (!hasGeneration_ || generation_ != clockGeneration) {
        ResetHistory();
        generation_ = clockGeneration;
        hasGeneration_ = true;
        Append(serial, authoritative);
        return hands::PublishKind::resetGeneration;
    }
    if (count_ != 0) {
        const Snapshot& latest = At(count_ - 1);
        if (serial < latest.serial) {
            ResetHistory();
            generation_ = clockGeneration;
            hasGeneration_ = true;
            Append(serial, authoritative);
            return hands::PublishKind::resetNonMonotonic;
        }
        if (serial == latest.serial) {
            if (hands::NearlyEqual(latest.matrix, authoritative)) {
                return hands::PublishKind::unchanged;
            }
            ResetHistory();
            generation_ = clockGeneration;
            hasGeneration_ = true;
            Append(serial, authoritative);
            return hands::PublishKind::resetSameTickMutation;
        }
        const std::uint64_t delta = serial - latest.serial;
        hands::Matrix rigidDelta{};
        if (!RigidDelta(latest.matrix, authoritative, rigidDelta)) {
            ResetHistory();
            generation_ = clockGeneration;
            hasGeneration_ = true;
            Append(serial, authoritative);
            return hands::PublishKind::invalid;
        }
        if (delta > 1 ||
            IsDiscontinuous(latest.matrix, authoritative, delta)) {
            ResetHistory();
            generation_ = clockGeneration;
            hasGeneration_ = true;
            Append(serial, authoritative);
            return hands::PublishKind::resetDiscontinuity;
        }
    }
    Append(serial, authoritative);
    return hands::PublishKind::appended;
}

hands::SampleResult ModelTrack::Evaluate(
    const clock::Sample& sample) const noexcept {
    if (sample.kind != clock::SampleKind::interpolated ||
        sample.olderSerial == sample.newerSerial) {
        return {hands::SampleKind::clockUnavailable, {}};
    }
    if (!hasGeneration_ || generation_ != sample.generation) {
        return {hands::SampleKind::generationMismatch, {}};
    }
    const Snapshot* older = nullptr;
    const Snapshot* newer = nullptr;
    for (std::size_t index = 0; index < count_; ++index) {
        const Snapshot& candidate = At(index);
        if (candidate.serial == sample.olderSerial) older = &candidate;
        if (candidate.serial == sample.newerSerial) newer = &candidate;
    }
    if (older == nullptr || newer == nullptr) {
        return {hands::SampleKind::snapshotsMissing, {}};
    }
    hands::Matrix interpolated{};
    if (!InterpolateAffineMotion(older->matrix, newer->matrix,
                                 sample.alpha, interpolated)) {
        return {hands::SampleKind::invalid, {}};
    }
    return {hands::SampleKind::applied, interpolated};
}

bool IsUsableAffine(const hands::Matrix& matrix) noexcept {
    for (const float value : matrix.values) {
        if (!std::isfinite(value)) return false;
    }
    if (std::abs(matrix.values[12]) > kAffineFooterEpsilon ||
        std::abs(matrix.values[13]) > kAffineFooterEpsilon ||
        std::abs(matrix.values[14]) > kAffineFooterEpsilon ||
        std::abs(matrix.values[15] - 1.0F) > kAffineFooterEpsilon) {
        return false;
    }
    const auto rowLength = [&matrix](const int row) {
        const float x = matrix.values[row * 4];
        const float y = matrix.values[row * 4 + 1];
        const float z = matrix.values[row * 4 + 2];
        return std::sqrt(x * x + y * y + z * z);
    };
    const float lengthProduct =
        rowLength(0) * rowLength(1) * rowLength(2);
    if (!std::isfinite(lengthProduct) || lengthProduct < 1.0e-12F ||
        lengthProduct > 1.0e12F) {
        return false;
    }
    const auto& m = matrix.values;
    const float determinant =
        m[0] * (m[5] * m[10] - m[6] * m[9]) -
        m[1] * (m[4] * m[10] - m[6] * m[8]) +
        m[2] * (m[4] * m[9] - m[5] * m[8]);
    return std::isfinite(determinant) &&
           std::abs(determinant) > 1.0e-5F * lengthProduct;
}

bool InterpolateAffineMotion(const hands::Matrix& older,
                             const hands::Matrix& newer,
                             const double alpha,
                             hands::Matrix& result) noexcept {
    if (!std::isfinite(alpha) || alpha < 0.0 || alpha > 1.0) return false;
    hands::Matrix delta{};
    if (!RigidDelta(older, newer, delta)) return false;
    // The relative delta's translation is expressed about the world origin.
    // Interpolating it and then composing with an object far from that origin
    // would create a false orbit while the object rotates. Interpolate only
    // the relative orientation here, then interpolate the final model origin
    // directly below.
    delta.values[3] = 0.0F;
    delta.values[7] = 0.0F;
    delta.values[11] = 0.0F;
    hands::Matrix interpolatedDelta{};
    if (!hands::Interpolate(hands::Identity(), delta, alpha,
                            interpolatedDelta) ||
        !hands::Multiply(interpolatedDelta, older, result)) {
        return false;
    }
    const float t = static_cast<float>(alpha);
    result.values[3] = older.values[3] +
                       (newer.values[3] - older.values[3]) * t;
    result.values[7] = older.values[7] +
                       (newer.values[7] - older.values[7]) * t;
    result.values[11] = older.values[11] +
                        (newer.values[11] - older.values[11]) * t;
    return IsUsableAffine(result);
}

} // namespace soma::world
