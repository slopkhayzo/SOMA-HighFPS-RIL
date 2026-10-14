#include "world_interpolation.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void Require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

soma::hands::Matrix At(const float x) {
    auto matrix = soma::hands::Identity();
    matrix.values[3] = x;
    return matrix;
}

soma::hands::Matrix LocalScale(const float x, const float y,
                               const float z) {
    auto matrix = soma::hands::Identity();
    matrix.values[0] = x;
    matrix.values[5] = y;
    matrix.values[10] = z;
    return matrix;
}

soma::hands::Matrix Root(const float x, const float yaw) {
    auto matrix = soma::hands::Identity();
    matrix.values[0] = std::cos(yaw);
    matrix.values[2] = std::sin(yaw);
    matrix.values[8] = -std::sin(yaw);
    matrix.values[10] = std::cos(yaw);
    matrix.values[3] = x;
    return matrix;
}

soma::hands::Matrix Product(const soma::hands::Matrix& left,
                            const soma::hands::Matrix& right) {
    soma::hands::Matrix result{};
    Require(soma::hands::Multiply(left, right, result),
            "test matrix multiplication failed");
    return result;
}

soma::clock::Sample Pair(const std::uint64_t older,
                         const std::uint64_t newer) {
    soma::clock::Sample sample{};
    sample.kind = soma::clock::SampleKind::interpolated;
    sample.generation = 7;
    sample.olderSerial = older;
    sample.newerSerial = newer;
    sample.alpha = 0.5;
    return sample;
}

} // namespace

int main() {
    using soma::world::Eligibility;
    Require(soma::world::ClassifyEligibility(true, 1, 0, false, 20, 19,
                                             true) == Eligibility::eligible,
            "recent one-body prop should be eligible");
    Require(soma::world::ClassifyEligibility(true, 2, 0, false, 20, 20,
                                             true) == Eligibility::eligible,
            "two-body articulated prop should be eligible");
    Require(soma::world::ClassifyEligibility(true, 27, 0, false, 20, 20,
                                             true) == Eligibility::eligible,
            "many-body debris such as the 27-shard window should be eligible");
    Require(soma::world::ClassifyEligibility(true, 65, 0, false, 20, 20,
                                             true) ==
                Eligibility::wrongBodyCount,
            "body counts beyond the validated layout bound fail closed");
    Require(soma::world::ClassifyEligibility(true, 0, 0, false, 20, 20,
                                             true) ==
                Eligibility::wrongBodyCount,
            "zero-body prop that nothing moves should remain excluded");
    Require(soma::world::ClassifyEligibility(true, 0, 0, true, 20, 20,
                                             true) == Eligibility::eligible,
            "zero-body prop moved by script or attachment should be eligible");
    Require(soma::world::ClassifyEligibility(true, 12, 12, false, 20, 20,
                                             true) == Eligibility::ragdoll,
            "skeleton-physics props stay stock until ragdoll palettes exist");
    Require(soma::world::ClassifyEligibility(true, 2, 1, false, 20, 20,
                                             true) == Eligibility::eligible,
            "previously accepted two-body props keep their path");
    Require(soma::world::ClassifyEligibility(true, 1, 0, false, 20, 17,
                                             true) == Eligibility::stale,
            "sleeping/stale prop should leave the hot path");
    Require(soma::world::ClassifyEligibility(true, 1, 0, false, 20, 21,
                                             true) == Eligibility::stale,
            "future registry serial should fail closed");

    soma::world::ModelTrack track;
    Require(track.Capture(0x1000, 10, 7, At(0.0F)) ==
                soma::hands::PublishKind::resetGeneration,
            "first model endpoint should establish the clock generation");
    Require(track.Capture(0x1000, 11, 7, At(2.0F)) ==
                soma::hands::PublishKind::appended,
            "second model endpoint should append");
    const auto midpoint = track.Evaluate(Pair(10, 11));
    Require(midpoint.kind == soma::hands::SampleKind::applied &&
                midpoint.matrix.values[3] > 0.99F &&
                midpoint.matrix.values[3] < 1.01F,
            "world model midpoint should interpolate");
    Require(track.Capture(0x1000, 11, 7, At(3.0F)) ==
                soma::hands::PublishKind::resetSameTickMutation,
            "same-tick model mutation should reset the history");
    Require(track.Evaluate(Pair(10, 11)).kind ==
                soma::hands::SampleKind::snapshotsMissing,
            "same-tick mutation must not render stale interpolation");
    Require(track.Capture(0x2000, 12, 7, At(4.0F)) ==
                soma::hands::PublishKind::resetGeneration,
            "owner replacement should establish a fresh endpoint");
    Require(track.Evaluate(Pair(11, 12)).kind ==
                soma::hands::SampleKind::snapshotsMissing,
            "owner replacement must not join histories");

    const auto combLocal = LocalScale(0.01F, 0.01F, 0.01F);
    const auto combOlder = Product(Root(0.0F, 0.0F), combLocal);
    const auto combNewer = Product(Root(2.0F, 0.5F), combLocal);
    soma::hands::Matrix affineMidpoint{};
    Require(soma::world::IsUsableAffine(combOlder) &&
                soma::world::InterpolateAffineMotion(
                    combOlder, combNewer, 0.5, affineMidpoint),
            "tiny comb-scale endpoint should interpolate");
    Require(std::abs(affineMidpoint.values[3] - 1.0F) < 1.0e-4F,
            "tiny-scale translation did not interpolate");

    const auto penLocal = LocalScale(0.824504F, 0.824504F, 6.19228F);
    const auto penOlder = Product(Root(0.0F, -0.25F), penLocal);
    const auto penNewer = Product(Root(2.0F, 0.25F), penLocal);
    Require(!soma::hands::IsRigid(penOlder) &&
                soma::world::IsUsableAffine(penOlder) &&
                soma::world::InterpolateAffineMotion(
                    penOlder, penNewer, 0.5, affineMidpoint),
            "non-uniform pen-scale endpoint should interpolate");
    Require(std::abs(affineMidpoint.values[3] - 1.0F) < 1.0e-4F,
            "non-uniform endpoint translation did not interpolate");

    const auto farOlder = Product(Root(1000.0F, -0.25F), penLocal);
    const auto farNewer = Product(Root(1002.0F, 0.25F), penLocal);
    Require(soma::world::InterpolateAffineMotion(
                farOlder, farNewer, 0.5, affineMidpoint) &&
                std::abs(affineMidpoint.values[3] - 1001.0F) < 1.0e-3F,
            "rotation must not make a distant object orbit the world origin");

    auto changedScale = penNewer;
    changedScale.values[10] *= 1.1F;
    Require(!soma::world::InterpolateAffineMotion(
                penOlder, changedScale, 0.5, affineMidpoint),
            "changing local affine terms must fail closed");

    std::cout << "world interpolation tests passed\n";
    return EXIT_SUCCESS;
}
