#include "camera_interpolation.h"

#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

int g_failures = 0;

void Expect(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

} // namespace

int main() {
    using namespace soma;
    camera::Pose older{};
    older.position = {0.0F, 1.0F, 2.0F};
    older.yaw = 3.10F;
    older.extendedRoll = -0.2F;
    camera::Pose newer = older;
    newer.position = {2.0F, 3.0F, 4.0F};
    newer.yaw = -3.10F;
    newer.extendedRoll = 0.2F;

    camera::Pose midpoint{};
    Expect(camera::Interpolate(older, newer, 0.5, midpoint),
           "valid poses should interpolate");
    Expect(std::abs(midpoint.position.x - 1.0F) < 1.0e-4F,
           "position midpoint");
    Expect(std::abs(std::abs(midpoint.yaw) - 3.14159265F) < 1.0e-3F,
           "yaw should take the shortest arc");
    Expect(std::abs(midpoint.extendedRoll) < 1.0e-4F,
           "extended rotation midpoint");

    camera::History history{};
    Expect(history.Publish(10, 3, older).kind ==
               camera::PublishKind::resetGeneration,
           "first publication establishes generation");
    Expect(history.Publish(11, 3, newer).kind == camera::PublishKind::appended,
           "adjacent publication appends");
    clock::Sample sample{};
    sample.kind = clock::SampleKind::interpolated;
    sample.generation = 3;
    sample.olderSerial = 10;
    sample.newerSerial = 11;
    sample.alpha = 0.5;
    Expect(history.Evaluate(sample).kind == camera::SampleKind::interpolated,
           "matching clock pair evaluates");

    camera::Pose teleport = newer;
    teleport.position.x += 100.0F;
    Expect(history.Publish(12, 3, teleport).kind ==
               camera::PublishKind::resetDiscontinuity,
           "teleport resets history");

    // Matrix rotate mode drives authored cameras (sitting, scan sequence).
    const auto yawMatrix = [](const float angle) {
        hands::Matrix matrix = hands::Identity();
        matrix.values[0] = std::cos(angle);
        matrix.values[2] = std::sin(angle);
        matrix.values[8] = -std::sin(angle);
        matrix.values[10] = std::cos(angle);
        return matrix;
    };
    camera::Pose authoredOlder{};
    authoredOlder.rotateMode = camera::kRotateModeMatrix;
    authoredOlder.rotation = yawMatrix(0.2F);
    authoredOlder.yaw = 1.5F;
    camera::Pose authoredNewer = authoredOlder;
    authoredNewer.position = {1.0F, 0.0F, 0.0F};
    authoredNewer.rotation = yawMatrix(0.4F);
    Expect(camera::IsFinite(authoredOlder),
           "rigid matrix-mode pose should be accepted");
    camera::Pose authoredMidpoint{};
    Expect(camera::Interpolate(authoredOlder, authoredNewer, 0.5,
                               authoredMidpoint),
           "matrix-mode poses should interpolate");
    Expect(hands::NearlyEqual(authoredMidpoint.rotation, yawMatrix(0.3F),
                              1.0e-4F),
           "matrix-mode rotation midpoint");
    Expect(std::abs(authoredMidpoint.position.x - 0.5F) < 1.0e-4F,
           "matrix-mode position midpoint");
    Expect(authoredMidpoint.yaw == authoredNewer.yaw,
           "matrix mode leaves Euler fields authoritative");

    camera::History authored{};
    Expect(authored.Publish(20, 3, older).kind ==
               camera::PublishKind::resetGeneration,
           "Euler pose starts a history");
    Expect(authored.Publish(21, 3, authoredOlder).kind ==
               camera::PublishKind::resetDiscontinuity,
           "switching rotate mode is a cut");
    Expect(authored.Publish(22, 3, authoredNewer).kind ==
               camera::PublishKind::appended,
           "consecutive matrix-mode poses append");

    camera::Pose sheared = authoredOlder;
    sheared.rotation.values[1] = 0.5F;
    Expect(!camera::IsFinite(sheared),
           "non-rigid matrix-mode rotation fails closed");
    camera::Pose unsupported = older;
    unsupported.rotateMode = 2;
    Expect(!camera::IsFinite(unsupported),
           "unknown rotate mode fails closed");

    // A previewed look must carry first-person content with the camera.
    camera::Pose lookInterpolated{};
    lookInterpolated.position = {3.0F, 1.5F, -2.0F};
    lookInterpolated.yaw = 0.4F;
    lookInterpolated.pitch = -0.1F;
    camera::Pose lookRendered = lookInterpolated;
    lookRendered.yaw = 0.55F;
    lookRendered.pitch = 0.05F;
    hands::Matrix lookCorrection{};
    Expect(camera::LookCorrection(lookRendered, lookInterpolated,
                                  lookCorrection),
           "look correction should build for Euler poses");
    const auto pointInFront = [](const camera::Pose& pose) {
        // The camera looks down its local -Z axis.
        const hands::Matrix world = camera::WorldRotation(
            pose.pitch, pose.yaw, pose.roll);
        return camera::Vec3{pose.position.x - world.values[2],
                            pose.position.y - world.values[6],
                            pose.position.z - world.values[10]};
    };
    const camera::Vec3 before = pointInFront(lookInterpolated);
    const camera::Vec3 expected = pointInFront(lookRendered);
    const auto& m = lookCorrection.values;
    const camera::Vec3 moved{
        m[0] * before.x + m[1] * before.y + m[2] * before.z + m[3],
        m[4] * before.x + m[5] * before.y + m[6] * before.z + m[7],
        m[8] * before.x + m[9] * before.y + m[10] * before.z + m[11]};
    Expect(std::abs(moved.x - expected.x) < 1.0e-4F &&
               std::abs(moved.y - expected.y) < 1.0e-4F &&
               std::abs(moved.z - expected.z) < 1.0e-4F,
           "content ahead of the interpolated camera stays ahead of the "
           "rendered camera");
    const hands::Matrix yawOnly = camera::WorldRotation(0.0F, 0.3F, 0.0F);
    Expect(std::abs(yawOnly.values[2] - std::sin(0.3F)) < 1.0e-6F &&
               std::abs(yawOnly.values[8] + std::sin(0.3F)) < 1.0e-6F,
           "yaw rotation matches the engine's RotY layout");
    camera::Pose authoredLook = lookRendered;
    authoredLook.rotateMode = camera::kRotateModeMatrix;
    Expect(!camera::LookCorrection(authoredLook, lookInterpolated,
                                   lookCorrection),
           "matrix-mode cameras get no look correction");

    if (g_failures != 0) return 1;
    std::cout << "camera interpolation tests passed\n";
    return 0;
}
