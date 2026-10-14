#include "presentation_clock.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

namespace {

int g_failures = 0;

void Expect(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

void TestCadence(const unsigned int simulationRate,
                 const unsigned int presentationRate) {
    using soma::clock::Configuration;
    using soma::clock::PresentationClock;
    using soma::clock::SampleKind;

    const double fixedStep = 1.0 / static_cast<double>(simulationRate);
    PresentationClock clock(Configuration{fixedStep, 0.250, 1});
    Expect(clock.IsConfigured(), "valid runtime clock configuration rejected");

    const double renderStep = 1.0 / static_cast<double>(presentationRate);
    std::uint64_t serial = 0;
    double latestTickTime = 0.0;
    double nextTickTime = fixedStep;
    double previousDisplayed = -1.0;
    unsigned int valid = 0;

    (void)clock.Publish(serial, 0.0);
    for (double now = 0.0; now <= 4.0; now += renderStep) {
        while (nextTickTime <= now + 1.0e-9) {
            latestTickTime = nextTickTime;
            ++serial;
            const auto published = clock.Publish(serial, now);
            Expect(published.accepted, "fixed endpoint should publish");
            nextTickTime += fixedStep;
        }
        const double alpha = (now - latestTickTime) / fixedStep;
        const auto sample = clock.Evaluate(now, alpha);
        if (sample.kind != SampleKind::interpolated) {
            continue;
        }
        Expect(sample.newerSerial == serial,
               "presentation pair must end at current fixed serial");
        Expect(sample.olderSerial + 1U == sample.newerSerial,
               "presentation must retain adjacent states only");
        Expect(sample.alpha >= 0.0 && sample.alpha <= 1.0,
               "alpha must remain bounded");
        const double displayed =
            static_cast<double>(sample.olderSerial) + sample.alpha;
        Expect(displayed + 1.0e-8 >= previousDisplayed,
               "displayed fixed time must not regress");
        previousDisplayed = displayed;
        ++valid;
    }
    Expect(valid > presentationRate * 3U,
           "cadence did not produce enough two-state samples");
}

void TestRuntimeConfiguration() {
    using soma::clock::Configuration;
    using soma::clock::PresentationClock;
    using soma::clock::ResetReason;
    using soma::clock::SampleKind;

    PresentationClock invalid(Configuration{});
    Expect(!invalid.IsConfigured(), "zero fixed step should be rejected");
    const auto rejected = invalid.Publish(0, 0.0);
    Expect(!rejected.accepted &&
               rejected.resetReason == ResetReason::invalidConfiguration,
           "invalid clock must fail closed");

    const double step = 1.0 / 120.0;
    PresentationClock clock(Configuration{step, 0.5, 1});
    (void)clock.Publish(10, 0.0);
    (void)clock.Publish(11, step);
    const auto midpoint = clock.Evaluate(1.5 * step, 0.5);
    Expect(midpoint.kind == SampleKind::interpolated &&
               std::abs(midpoint.targetTime - 10.5 * step) < 1.0e-10,
           "target time must use the configured native step");

    const auto missing = clock.Publish(13, 2.0 * step);
    Expect(missing.resetReason == ResetReason::missingSnapshots &&
               clock.SnapshotCount() == 1,
           "skipped serial must reset to the new endpoint");

    const auto invalidTime =
        clock.Publish(14, std::numeric_limits<double>::quiet_NaN());
    Expect(!invalidTime.accepted &&
               invalidTime.resetReason == ResetReason::nonMonotonicWallTime,
           "non-finite wall time must fail closed");
}

void TestLogicTimerResidual() {
    double alpha = -1.0;
    double raw = 0.0;
    Expect(soma::clock::LogicTimerAlpha(
               100.0, 1000.0 / 60.0, 1.0, 100.0 - 1000.0 / 120.0,
               0.0, alpha, &raw) &&
               std::abs(alpha - 0.5) < 1.0e-8 &&
               std::abs(raw - 0.5) < 1.0e-8,
           "60 Hz logic-timer midpoint was incorrect");
    Expect(soma::clock::LogicTimerAlpha(
               100.0, 1000.0 / 120.0, 1.0, 100.0 - 1000.0 / 240.0,
               0.0, alpha) &&
               std::abs(alpha - 0.5) < 1.0e-8,
           "120 Hz logic-timer midpoint was incorrect");
    Expect(!soma::clock::LogicTimerAlpha(100.0, 0.0, 1.0, 100.0, 0.0, alpha),
           "invalid timer step must fail closed");
}

} // namespace

int main() {
    for (const unsigned int rate :
         {60U, 90U, 119U, 120U, 121U, 144U, 165U, 240U, 360U}) {
        TestCadence(60, rate);
    }
    for (const unsigned int rate : {120U, 144U, 165U, 239U, 240U, 241U, 360U}) {
        TestCadence(120, rate);
    }
    TestRuntimeConfiguration();
    TestLogicTimerResidual();

    if (g_failures != 0) {
        std::cerr << g_failures << " presentation-clock assertion(s) failed\n";
        return 1;
    }
    std::cout << "runtime-configured two-state presentation tests passed\n";
    return 0;
}

