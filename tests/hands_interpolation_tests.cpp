#include "hands_interpolation.h"

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

soma::hands::Matrix Transform(const float x, const float yaw = 0.0F,
                              const float scale = 1.0F) {
    auto matrix = soma::hands::Identity();
    matrix.values[0] = std::cos(yaw) * scale;
    matrix.values[2] = std::sin(yaw) * scale;
    matrix.values[5] = scale;
    matrix.values[8] = -std::sin(yaw) * scale;
    matrix.values[10] = std::cos(yaw) * scale;
    matrix.values[3] = x;
    return matrix;
}

soma::clock::Sample Pair(const std::uint64_t older,
                         const std::uint64_t newer, const double alpha) {
    soma::clock::Sample sample{};
    sample.kind = soma::clock::SampleKind::interpolated;
    sample.generation = 3;
    sample.olderSerial = older;
    sample.newerSerial = newer;
    sample.alpha = alpha;
    return sample;
}

} // namespace

int main() {
    const auto older = Transform(0.0F, 3.12413936F, 0.25F);
    const auto newer = Transform(2.0F, -3.12413936F, 0.25F);
    soma::hands::Matrix midpoint{};
    Require(soma::hands::Interpolate(older, newer, 0.5, midpoint),
            "uniformly scaled root interpolation failed");
    Require(std::abs(midpoint.values[3] - 1.0F) < 1.0e-5F,
            "root translation did not interpolate");
    soma::hands::Matrix inverse{};
    soma::hands::Matrix identity{};
    Require(soma::hands::InverseRigid(newer, inverse) &&
                soma::hands::Multiply(newer, inverse, identity) &&
                soma::hands::NearlyEqual(identity, soma::hands::Identity(),
                                         2.0e-4F),
            "uniform-scale inverse did not compose to identity");

    soma::hands::History history;
    Require(history.Publish(10, 3, older) ==
                soma::hands::PublishKind::resetGeneration,
            "first root did not establish generation");
    Require(history.Publish(11, 3, newer) ==
                soma::hands::PublishKind::appended,
            "second root did not append");
    Require(history.Evaluate(Pair(10, 11, 0.5)).kind ==
                soma::hands::SampleKind::applied,
            "root history pair was not sampled");
    std::cout << "hands interpolation tests passed\n";
    return EXIT_SUCCESS;
}
