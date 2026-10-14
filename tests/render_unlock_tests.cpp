#include "runtime.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

struct FakeEngine {
    std::array<std::uint8_t, 0x78> padding{};
    std::uint8_t limitFps = 1;
};

std::atomic<std::uint8_t> g_observedLimitFps{0xff};

__declspec(noinline) void FakeEngineRun(void* const self) {
    const auto* const engine = static_cast<const FakeEngine*>(self);
    g_observedLimitFps.store(engine->limitFps, std::memory_order_release);
    Sleep(0);
}

} // namespace

int main() {
    static_assert(offsetof(FakeEngine, limitFps) == 0x78);

    soma::target::RuntimeMappings mappings{};
    mappings.engineRun = reinterpret_cast<std::uintptr_t>(&FakeEngineRun);
    mappings.limitFpsOffset =
        static_cast<std::uint8_t>(offsetof(FakeEngine, limitFps));

    std::wstring error;
    if (!soma::runtime::InitializeRenderUnlock(mappings, error)) {
        std::wcerr << L"render unlock initialization failed: " << error << L'\n';
        return 1;
    }

    FakeEngine engine{};
    FakeEngineRun(&engine);
    if (g_observedLimitFps.load(std::memory_order_acquire) != 0) {
        std::cerr << "detour did not clear LimitFPS before the original call\n";
        return 1;
    }
    if (engine.limitFps != 1) {
        std::cerr << "detour did not restore LimitFPS after the original call\n";
        return 1;
    }

    std::cout << "render unlock detour and restoration test passed\n";
    return 0;
}
