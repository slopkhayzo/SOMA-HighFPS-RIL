#include "pose_interpolation.h"

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

soma::pose::Pose MakePose(const float x, const std::size_t count = 2) {
    soma::pose::Pose pose{};
    pose.boneCount = count;
    for (std::size_t index = 0; index < count; ++index) {
        pose.bones[index] = soma::hands::Identity();
        pose.bones[index].values[3] = x + static_cast<float>(index);
    }
    return pose;
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
    soma::pose::History history;
    Require(history.Publish(4, 7, MakePose(0.0F)) ==
                soma::pose::PublishKind::resetGeneration,
            "first pose did not establish generation");
    Require(history.Publish(5, 7, MakePose(2.0F)) ==
                soma::pose::PublishKind::appended,
            "second pose did not append");
    const auto sampled = history.Evaluate(Pair(4, 5));
    Require(sampled.kind == soma::pose::SampleKind::applied &&
                std::abs(sampled.pose.bones[0].values[3] - 1.0F) < 1.0e-5F,
            "pose history did not interpolate atomically");
    Require(history.Publish(6, 7, MakePose(3.0F, 3)) ==
                soma::pose::PublishKind::resetBoneCount,
            "bone-count change did not reset history");
    std::cout << "pose interpolation tests passed\n";
    return EXIT_SUCCESS;
}
