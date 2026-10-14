#include "config.h"

#include <Windows.h>

#include <filesystem>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void Expect(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

std::filesystem::path TempIni() {
    wchar_t directory[MAX_PATH]{};
    wchar_t file[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, directory) == 0 ||
        GetTempFileNameW(directory, L"sff", 0, file) == 0) {
        return {};
    }
    return file;
}

bool Set(const std::filesystem::path& path, const wchar_t* section,
         const wchar_t* key, const wchar_t* value) {
    return WritePrivateProfileStringW(section, key, value, path.c_str()) != FALSE;
}

} // namespace

int main() {
    const auto missing = soma::config::Load(L"Z:\\not-present\\SOMA.FPSFix.ini");
    Expect(!missing.ok, "missing INI should fail closed");

    const std::filesystem::path path = TempIni();
    Expect(!path.empty(), "temporary INI path should be created");
    if (!path.empty()) {
        Expect(Set(path, L"General", L"Enabled", L"true"), "write Enabled");
        Expect(Set(path, L"Logging", L"HealthReports", L"false"),
               "write HealthReports");
        Expect(Set(path, L"Logging", L"DetailedReports", L"true"),
               "write DetailedReports");
        Expect(Set(path, L"Features", L"ToolInterpolation", L"true"),
               "write ToolInterpolation");
        Expect(Set(path, L"Features", L"GuiMouseOverlay", L"yes"),
               "write GuiMouseOverlay");
        Expect(Set(path, L"Features", L"AgentPoseInterpolation", L"on"),
               "write AgentPoseInterpolation");
        Expect(Set(path, L"Features", L"AgentRootInterpolation", L"on"),
               "write AgentRootInterpolation");
        Expect(Set(path, L"Timing", L"TargetFPS", L"240"), "write TargetFPS");

        const auto loaded = soma::config::Load(path.wstring());
        Expect(loaded.ok, "valid INI should load");
        Expect(loaded.settings.features.toolInterpolation,
               "ToolInterpolation should parse");
        Expect(loaded.settings.features.guiMouseOverlay,
               "GuiMouseOverlay should parse");
        Expect(loaded.settings.features.agentPoseInterpolation,
               "AgentPoseInterpolation should parse");
        Expect(loaded.settings.features.agentRootInterpolation,
               "AgentRootInterpolation should parse");
        Expect(loaded.settings.targetFps == 240, "TargetFPS should parse");
        Expect(!loaded.settings.healthReports && loaded.settings.detailedReports,
               "Logging options should parse");
        Expect(soma::config::AnySmoothingFeatureEnabled(loaded.settings.features),
               "smoothing features need the timing hooks");
        soma::config::Features unlockOnly{};
        unlockOnly.renderUnlock = true;
        Expect(soma::config::AnyBehavioralFeatureEnabled(unlockOnly) &&
                   !soma::config::AnySmoothingFeatureEnabled(unlockOnly),
               "render unlock alone needs no timing hooks");
        Expect(soma::config::AnyBehavioralFeatureEnabled(loaded.settings.features),
               "enabled feature should be detected");
        Expect(soma::config::AnyRuntimeWorkEnabled(loaded.settings),
               "runtime work should include behavioral features");

        Expect(Set(path, L"Features", L"MouseOverlay", L"perhaps"),
               "write invalid boolean");
        const auto invalid = soma::config::Load(path.wstring());
        Expect(!invalid.ok, "invalid boolean should fail closed");
        std::error_code ignored;
        (void)std::filesystem::remove(path, ignored);
    }

    if (g_failures != 0) {
        std::cerr << g_failures << " config assertion(s) failed\n";
        return 1;
    }
    std::cout << "configuration parsing and fail-closed tests passed\n";
    return 0;
}

