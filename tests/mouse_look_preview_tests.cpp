#include "mouse_look_preview.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {

int g_failures = 0;

void Expect(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

constexpr double kScale = -0.0012;

} // namespace

int main() {
    using namespace soma;

    // Conservation: counts read early reach the engine exactly once.
    mouse::LookPreview conserve{};
    conserve.OnAcquired(5, -2);
    conserve.OnAcquired(3, 1);
    Expect(conserve.Pending().x == 8 && conserve.Pending().y == -1,
           "early reads accumulate as pending");
    const mouse::Counts handed = conserve.OnDelivered(4, 7);
    Expect(handed.x == 12 && handed.y == 6,
           "the engine receives its own read plus everything read early");
    Expect(conserve.Pending().x == 0 && conserve.Pending().y == 0,
           "delivery retires pending counts");
    const mouse::Counts second = conserve.OnDelivered(1, 1);
    Expect(second.x == 1 && second.y == 1, "nothing is delivered twice");
    Expect(conserve.Acquired().x == 8 && conserve.Delivered().x == 13,
           "acquired and delivered totals are tracked");
    conserve.OnAcquired(9, 9);
    conserve.OnDiscarded();
    Expect(conserve.Pending().x == 0,
           "an engine flush also discards early reads");

    // Direct look, lag 0: the camera turns in the tick that got the counts.
    mouse::LookPreview direct{};
    double yaw = -3.10;
    double pitch = 0.0;
    direct.OnTickBoundary(true, yaw, pitch);
    Expect(!direct.PendingOffset().yawValid,
           "no preview before a scale is learned");
    const int motion[] = {7, 12, 5, 20, 9, 14};
    for (const int counts : motion) {
        (void)direct.OnDelivered(counts, -counts);
        yaw = mouse::WrapAngle(yaw + kScale * counts);
        pitch += kScale * -counts;
        direct.OnTickBoundary(true, yaw, pitch);
    }
    Expect(direct.Yaw(0).Direct() && direct.Pitch(0).Direct(),
           "a linear response is recognized as direct");
    Expect(std::abs(direct.Yaw(0).Scale() - kScale) < 1.0e-9,
           "radians per count is learned across the yaw wrap");
    direct.OnAcquired(10, 4);
    mouse::Offset offset = direct.PendingOffset();
    Expect(offset.yawValid && std::abs(offset.yaw - kScale * 10) < 1.0e-9,
           "pending counts preview as yaw");
    Expect(offset.pitchValid && std::abs(offset.pitch - kScale * 4) < 1.0e-9,
           "pending counts preview as pitch");

    // The tick consumes the pending counts; the preview returns to zero and
    // the authoritative angle now contains exactly what was previewed.
    (void)direct.OnDelivered(0, 0);
    yaw = mouse::WrapAngle(yaw + kScale * 10);
    pitch += kScale * 4;
    direct.OnTickBoundary(true, yaw, pitch);
    offset = direct.PendingOffset();
    Expect(offset.yawValid && offset.yaw == 0.0,
           "preview is continuous across the fixed update");

    // A pitch limit clamps the response: that axis disengages on its own.
    (void)direct.OnDelivered(8, 30);
    yaw = mouse::WrapAngle(yaw + kScale * 8);
    direct.OnTickBoundary(true, yaw, pitch);
    offset = direct.PendingOffset();
    Expect(offset.yawValid && !offset.pitchValid,
           "a clamped axis stops previewing while the other continues");

    // A scripted turn with no mouse motion is not a direct command.
    yaw = mouse::WrapAngle(yaw + 0.05);
    direct.OnTickBoundary(true, yaw, pitch);
    Expect(!direct.PendingOffset().yawValid,
           "camera motion without counts disengages the preview");
    for (int tick = 0; tick < 5; ++tick) {
        direct.OnTickBoundary(true, yaw, pitch);
    }
    Expect(!direct.PendingOffset().yawValid,
           "idle ticks alone do not re-arm the preview");
    (void)direct.OnDelivered(6, 0);
    yaw = mouse::WrapAngle(yaw + kScale * 6);
    direct.OnTickBoundary(true, yaw, pitch);
    Expect(direct.PendingOffset().yawValid,
           "mouse motion that turns the camera re-arms the preview");

    // Rotating a readable: the mouse moves but the camera does not. After
    // the first such tick the preview must stay off through every pause and
    // restart, until the mouse turns the camera again.
    (void)direct.OnDelivered(15, 0);
    direct.OnTickBoundary(true, yaw, pitch);
    Expect(!direct.PendingOffset().yawValid,
           "motion that does not turn the camera disarms the preview");
    for (int restart = 0; restart < 3; ++restart) {
        for (int tick = 0; tick < 10; ++tick) {
            direct.OnTickBoundary(true, yaw, pitch);
        }
        direct.OnAcquired(12, 0);
        Expect(!direct.PendingOffset().yawValid,
               "restarting motion after a pause stays unpreviewed");
        (void)direct.OnDelivered(0, 0);
        direct.OnTickBoundary(true, yaw, pitch);
    }
    (void)direct.OnDelivered(9, 0);
    yaw = mouse::WrapAngle(yaw + kScale * 9);
    direct.OnTickBoundary(true, yaw, pitch);
    Expect(direct.PendingOffset().yawValid,
           "returning to mouse look arms the preview again");

    // Smoothed input is not linear in the current counts: never direct.
    mouse::LookPreview smoothed{};
    double filtered = 0.0;
    double smoothYaw = 0.0;
    smoothed.OnTickBoundary(true, smoothYaw, 0.0);
    const int burst[] = {20, 3, 17, 4, 25, 6, 11, 30, 5, 18};
    for (const int counts : burst) {
        (void)smoothed.OnDelivered(counts, 0);
        filtered = 0.6 * filtered + 0.4 * counts;
        smoothYaw += kScale * filtered;
        smoothed.OnTickBoundary(true, smoothYaw, 0.0);
    }
    Expect(!smoothed.PendingOffset().yawValid,
           "smoothed mouse input is not previewed");

    // Lag 1: counts delivered in one tick turn the camera in the next.
    mouse::LookPreview lagged{};
    double lagYaw = 0.0;
    int previous = 0;
    lagged.OnTickBoundary(true, lagYaw, 0.0);
    const int varied[] = {6, 15, 4, 22, 9, 13, 7};
    for (const int counts : varied) {
        (void)lagged.OnDelivered(counts, 0);
        lagYaw += kScale * previous;
        previous = counts;
        lagged.OnTickBoundary(true, lagYaw, 0.0);
    }
    Expect(!lagged.Yaw(0).Direct() && lagged.Yaw(1).Direct(),
           "a one-tick application delay is detected");
    lagged.OnAcquired(5, 0);
    offset = lagged.PendingOffset();
    Expect(offset.yawValid &&
               std::abs(offset.yaw - kScale * (5 + previous)) < 1.0e-9,
           "lagged preview includes counts delivered but not yet applied");

    // A 125 Hz mouse read at 350 Hz: each report is spread over one report
    // interval, so the previewed angle advances every frame instead of
    // jumping once every two or three frames.
    const auto primed = [] {
        mouse::LookPreview preview{};
        double primedYaw = 0.0;
        preview.OnTickBoundary(true, primedYaw, 0.0);
        const int samples[] = {7, 12, 5, 20, 9, 14};
        for (const int counts : samples) {
            (void)preview.OnDelivered(counts, 0);
            primedYaw += kScale * counts;
            preview.OnTickBoundary(true, primedYaw, 0.0);
        }
        return preview;
    };
    mouse::LookPreview slowMouse = primed();
    const double frame = 1.0 / 350.0;
    const double report = 1.0 / 125.0;
    double nextReport = report;
    double previousOffset = 0.0;
    double largestStep = 0.0;
    double smallestStep = 1.0;
    int steps = 0;
    for (int index = 1; index <= 140; ++index) {
        const double now = frame * index;
        int counts = 0;
        if (now >= nextReport) {
            counts = 8;
            nextReport += report;
        }
        slowMouse.OnAcquired(counts, 0, now);
        const mouse::Offset smooth = slowMouse.PendingOffset(now);
        if (index > 70) {
            const double step = std::abs(smooth.yaw - previousOffset);
            largestStep = (std::max)(largestStep, step);
            smallestStep = (std::min)(smallestStep, step);
            ++steps;
        }
        previousOffset = smooth.yaw;
    }
    const double evenStep = std::abs(kScale) * 8.0 * (frame / report);
    Expect(slowMouse.SmoothingActive(),
           "a mouse slower than the frame rate enables report smoothing");
    Expect(std::abs(slowMouse.ReportInterval() - report) < 0.0015,
           "the report interval is measured from the reads");
    Expect(steps > 0 && largestStep < evenStep * 1.6 &&
               smallestStep > evenStep * 0.4,
           "smoothed preview advances by a similar amount every frame");
    const mouse::Offset settled =
        slowMouse.PendingOffset(frame * 140 + report * 2);
    Expect(std::abs(settled.yaw -
                    kScale * static_cast<double>(slowMouse.Pending().x)) <
               1.0e-9,
           "all motion is shown once the report interval has passed");

    // The withheld part survives the engine consuming the counts.
    mouse::LookPreview acrossTick = primed();
    for (int index = 1; index <= 60; ++index) {
        const double now = frame * index;
        acrossTick.OnAcquired(index % 3 == 0 ? 8 : 0, 0, now);
    }
    const double tickTime = frame * 60;
    const double beforeTick = acrossTick.PendingOffset(tickTime).yaw;
    const mouse::Counts consumed = acrossTick.OnDelivered(0, 0, tickTime);
    acrossTick.OnTickBoundary(true, kScale * 67.0 + kScale * consumed.x, 0.0);
    const double afterTick = acrossTick.PendingOffset(tickTime).yaw;
    Expect(std::abs((kScale * consumed.x + afterTick) - beforeTick) < 1.0e-9,
           "smoothed preview is continuous across the fixed update");

    // A mouse that reports at least once per frame is shown unsmoothed.
    mouse::LookPreview fastMouse = primed();
    for (int index = 1; index <= 80; ++index) {
        fastMouse.OnAcquired(3, 0, frame * index);
    }
    Expect(!fastMouse.SmoothingActive(),
           "a mouse at or above the frame rate is not smoothed");
    Expect(std::abs(fastMouse.PendingOffset(frame * 80).yaw -
                    kScale * 240.0) < 1.0e-9,
           "unsmoothed preview shows every pending count immediately");

    // Frames that run a fixed update sample the mouse later than other
    // frames yet are shown on the same schedule. Holding back the late share
    // makes the shown motion advance evenly again.
    const auto frameSteps = [&primed](const bool compensate) {
        mouse::LookPreview preview = primed();
        const double grid = 1.0 / 350.0;
        const double late = 0.0018;
        const double countsPerSecond = 2000.0;
        double carried = 0.0;
        double sampledAt = 0.0;
        double shown = 0.0;
        double largest = 0.0;
        double smallest = 1.0;
        for (int index = 1; index <= 120; ++index) {
            const bool tickFrame = index % 6 == 0;
            const double now = grid * index + (tickFrame ? late : 0.0);
            carried += countsPerSecond * (now - sampledAt);
            const auto whole = static_cast<std::int64_t>(carried);
            carried -= static_cast<double>(whole);
            sampledAt = now;
            preview.OnAcquired(whole, 0, now,
                               compensate && tickFrame ? late : 0.0);
            const double offset = preview.PendingOffset(now).yaw;
            if (index > 12) {
                const double step = std::abs(offset - shown);
                largest = (std::max)(largest, step);
                smallest = (std::min)(smallest, step);
            }
            shown = offset;
        }
        return largest / smallest;
    };
    Expect(frameSteps(false) > 3.0,
           "uncompensated late sampling produces uneven steps");
    Expect(frameSteps(true) < 1.6,
           "holding back the late share evens the steps out");

    // An implausibly large pending turn is not trusted.
    direct.OnAcquired(100000, 0);
    Expect(!direct.PendingOffset().yawValid,
           "oversized pending motion fails closed");

    // Losing the camera invalidates the learned state.
    direct.OnTickBoundary(false, 0.0, 0.0);
    Expect(!direct.Yaw(0).HasScale(), "an invalid pose resets the model");

    if (g_failures != 0) return 1;
    std::cout << "mouse look preview tests passed\n";
    return 0;
}
