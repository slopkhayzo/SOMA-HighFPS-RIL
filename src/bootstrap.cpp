#include "bootstrap.h"

#include "config.h"
#include "logger.h"
#include "plugin_version.h"
#include "runtime.h"
#include "target_mapping.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace soma {
namespace {

enum class BootstrapState : unsigned int {
    dormant,
    starting,
    disabled,
    rejected,
    readyWithoutHooks,
    readyWithHooks,
};

std::atomic<BootstrapState> g_state{BootstrapState::dormant};

std::wstring ModulePath(const HMODULE module) {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD copied = GetModuleFileNameW(
            module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (copied == 0) {
            return {};
        }
        if (copied < buffer.size() - 1) {
            return std::wstring(buffer.data(), copied);
        }
        if (buffer.size() >= 32768) {
            return {};
        }
        buffer.resize(std::min<std::size_t>(buffer.size() * 2, 32768));
    }
}

std::wstring ReplaceExtension(const std::wstring& path,
                              const std::wstring_view extension) {
    const std::size_t slash = path.find_last_of(L"\\/");
    const std::size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos ||
        (slash != std::wstring::npos && dot < slash)) {
        return path + std::wstring(extension);
    }
    return path.substr(0, dot) + std::wstring(extension);
}

std::wstring LocalAppDataLogPath() {
    std::vector<wchar_t> buffer(32768);
    const DWORD copied = GetEnvironmentVariableW(
        L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (copied == 0 || copied >= buffer.size()) {
        return {};
    }

    std::wstring directory(buffer.data(), copied);
    directory.append(L"\\SOMA.FPSFix");
    if (!CreateDirectoryW(directory.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        return {};
    }
    return directory + L"\\SOMA.FPSFix.log";
}

std::wstring Hex(const std::uint64_t value) {
    std::wostringstream stream;
    stream << L"0x" << std::uppercase << std::hex << value;
    return stream.str();
}

void LogRequestedWork(const config::Settings& settings) {
    const auto boolText = [](const bool value) { return value ? L"true" : L"false"; };
    const auto& f = settings.features;
    log::Write(log::Level::debug,
               L"Requested runtime work: HealthReports=" +
                   std::wstring(boolText(settings.healthReports)) +
                   L", DetailedReports=" + boolText(settings.detailedReports) +
                   L", RenderUnlock=" + boolText(f.renderUnlock) +
                   L", PresentationLimiter=" + boolText(f.presentationLimiter) +
                   L", CameraInterpolation=" + boolText(f.cameraInterpolation) +
                   L", HandsRootInterpolation=" + boolText(f.handsRootInterpolation) +
                   L", HandsPoseInterpolation=" + boolText(f.handsPoseInterpolation) +
                   L", ToolInterpolation=" + boolText(f.toolInterpolation) +
                   L", WorldRootInterpolation=" + boolText(f.worldRootInterpolation) +
                   L", WorldPoseInterpolation=" + boolText(f.worldPoseInterpolation) +
                   L", AgentRootInterpolation=" + boolText(f.agentRootInterpolation) +
                   L", AgentPoseInterpolation=" + boolText(f.agentPoseInterpolation) +
                   L", RagdollPoseInterpolation=" + boolText(f.ragdollPoseInterpolation) +
                   L", ParticleInterpolation=" + boolText(f.particleInterpolation) +
                   L", ImageTrailTiming=" + boolText(f.imageTrailTiming) +
                   L", MouseOverlay=" + boolText(f.mouseOverlay) +
                   L", GuiMouseOverlay=" + boolText(f.guiMouseOverlay));
}

} // namespace

DWORD WINAPI BootstrapThread(void* moduleParameter) {
    g_state.store(BootstrapState::starting, std::memory_order_release);
    const HMODULE pluginModule = static_cast<HMODULE>(moduleParameter);
    const std::wstring pluginPath = ModulePath(pluginModule);
    if (pluginPath.empty()) {
        OutputDebugStringW(L"[SOMA.FPSFix] Could not resolve plugin path.\n");
        g_state.store(BootstrapState::rejected, std::memory_order_release);
        return 1;
    }

    const std::wstring iniPath = ReplaceExtension(pluginPath, L".ini");
    const std::wstring logPath = ReplaceExtension(pluginPath, L".log");
    if (!log::Initialize(logPath)) {
        const std::wstring fallback = LocalAppDataLogPath();
        if (fallback.empty() || !log::Initialize(fallback)) {
            OutputDebugStringW(
                L"[SOMA.FPSFix] Could not open sibling or fallback log.\n");
        } else {
            log::Write(log::Level::warning,
                       L"Could not write sibling log; using: " + fallback);
        }
    }

    log::Write(log::Level::info,
               std::wstring(SOMA_FPS_FIX_NAME) + L" " + SOMA_FPS_FIX_VERSION +
                   L" bootstrap starting (x64)");
    log::Write(log::Level::debug, L"Plugin path: " + pluginPath);
    log::Write(log::Level::debug, L"INI path: " + iniPath);

    const config::LoadResult loaded = config::Load(iniPath);
    if (!loaded.ok) {
        log::Write(log::Level::error, loaded.error);
        log::Write(log::Level::error,
                   L"Fail-closed: no engine hooks were installed.");
        g_state.store(BootstrapState::rejected, std::memory_order_release);
        return 2;
    }
    log::SetDiagnosticsEnabled(loaded.settings.diagnostics);
    if (!loaded.settings.enabled) {
        log::Write(log::Level::info,
                   L"Plugin disabled by [General] Enabled=false.");
        g_state.store(BootstrapState::disabled, std::memory_order_release);
        return 0;
    }
    LogRequestedWork(loaded.settings);

    const target::InspectionResult host =
        target::InspectMappedHost(GetModuleHandleW(nullptr));
    if (!host.ok) {
        log::Write(log::Level::error, L"Host inspection failed: " + host.error);
        log::Write(log::Level::error,
                   L"Fail-closed: no engine hooks were installed.");
        g_state.store(BootstrapState::rejected, std::memory_order_release);
        return 3;
    }
    log::Write(log::Level::info,
               L"Host PE accepted for runtime inspection: machine=" +
                   Hex(host.identity.machine) + L", timestamp=" +
                   Hex(host.identity.timestamp) + L", imageSize=" +
                   Hex(host.identity.imageSize) + L", codeSize=" +
                   Hex(host.identity.codeSize) + L", mappedBase=" +
                   Hex(host.identity.imageBase));

    const bool runtimeWorkRequested =
        config::AnyRuntimeWorkEnabled(loaded.settings);
    target::MappingResult runtimeMappings{};
    if (loaded.settings.diagnostics || runtimeWorkRequested) {
        runtimeMappings = target::ResolveRuntimeMappings(
            GetModuleHandleW(nullptr));
        if (!runtimeMappings.ok) {
            log::Write(runtimeWorkRequested ? log::Level::error
                                            : log::Level::warning,
                       L"SOMA runtime mapping rejected: " + runtimeMappings.error);
            if (runtimeWorkRequested) {
                log::Write(log::Level::error,
                           L"Fail-closed: no engine hooks were installed.");
                g_state.store(BootstrapState::rejected,
                              std::memory_order_release);
                return 4;
            }
        } else {
            const auto rva = [&](const std::uintptr_t address) {
                return address - host.identity.imageBase;
            };
            log::Write(
                log::Level::info,
                L"SOMA runtime mapping validated: cEngine::Run RVA=" +
                    Hex(rva(runtimeMappings.mappings.engineRun)) +
                    L", cUpdater::RunMessage RVA=" +
                    Hex(rva(runtimeMappings.mappings.updaterRunMessage)) +
                    L", engineMicroseconds RVA=" +
                    Hex(rva(runtimeMappings.mappings.engineMicroseconds)) +
                    L", cScene::Render RVA=" +
                    Hex(rva(runtimeMappings.mappings.sceneRender)) +
                    L", viewportRender RVA=" +
                    Hex(rva(runtimeMappings.mappings.viewportRender)) +
                    L", handsSetMatrix RVA=" +
                    Hex(rva(runtimeMappings.mappings.handsSetMatrix)) +
                    L", attachmentUpdate RVA=" +
                    Hex(rva(runtimeMappings.mappings.attachmentUpdate)) +
                    L", meshUpdateGraphics RVA=" +
                    Hex(rva(runtimeMappings.mappings.meshUpdateGraphics)) +
                    L", skinningConvert RVA=" +
                    Hex(rva(runtimeMappings.mappings.skinningConvert)) +
                    L", subMeshGetModelMatrix RVA=" +
                    Hex(rva(runtimeMappings.mappings.subMeshGetModelMatrix)) +
                    L", cLuxProp::OnUpdate RVA=" +
                    Hex(rva(runtimeMappings.mappings.luxPropOnUpdate)) +
                    L", cLuxAgent::OnUpdate RVA=" +
                    Hex(rva(runtimeMappings.mappings.luxAgentOnUpdate)) +
                    L", GUI pixel mouse setter RVA=" +
                    Hex(rva(runtimeMappings.mappings.guiMousePixelSetter)) +
                    L", GUI virtual mouse setter RVA=" +
                    Hex(rva(runtimeMappings.mappings.guiMouseVirtualSetter)) +
                    L", cImGui::Draw RVA=" +
                    Hex(rva(runtimeMappings.mappings.guiCursorQueue)) +
                    L", cImGui::DoMouse RVA=" +
                    Hex(rva(runtimeMappings.mappings.guiDoMouse)) +
                    L", renderGate RVA=" + Hex(rva(runtimeMappings.mappings.renderGate)) +
                    L", flushCall RVA=" + Hex(rva(runtimeMappings.mappings.flushCall)) +
                    L", presentCall RVA=" + Hex(rva(runtimeMappings.mappings.presentCall)) +
                    L", LimitFPS offset=" +
                    Hex(runtimeMappings.mappings.limitFpsOffset) +
                    L", timer offset=" +
                    Hex(runtimeMappings.mappings.engineTimerOffset) +
                    L", viewport camera offset=" +
                    Hex(runtimeMappings.mappings.viewportCameraOffset));
        }
    }

    config::Features unsupported = loaded.settings.features;
    unsupported.renderUnlock = false;
    unsupported.cameraInterpolation = false;
    unsupported.handsRootInterpolation = false;
    unsupported.handsPoseInterpolation = false;
    unsupported.worldRootInterpolation = false;
    unsupported.worldPoseInterpolation = false;
    unsupported.agentRootInterpolation = false;
    unsupported.agentPoseInterpolation = false;
    unsupported.guiMouseOverlay = false;
    unsupported.mouseOverlay = false;
    unsupported.ragdollPoseInterpolation = false;
    const bool unsupportedWorkRequested =
        config::AnyBehavioralFeatureEnabled(unsupported);
    if (unsupportedWorkRequested) {
        log::Write(
            log::Level::error,
            L"One or more requested runtime features are not implemented in this "
            L"milestone.");
        log::Write(log::Level::error,
                   L"Fail-closed: no engine hooks were installed.");
        g_state.store(BootstrapState::rejected, std::memory_order_release);
        return 5;
    }

    if (loaded.settings.features.renderUnlock ||
        loaded.settings.features.cameraInterpolation ||
        loaded.settings.features.handsRootInterpolation ||
        loaded.settings.features.handsPoseInterpolation ||
        loaded.settings.features.worldRootInterpolation ||
        loaded.settings.features.worldPoseInterpolation ||
        loaded.settings.features.agentRootInterpolation ||
        loaded.settings.features.agentPoseInterpolation ||
        loaded.settings.features.guiMouseOverlay ||
        loaded.settings.features.mouseOverlay ||
        loaded.settings.features.ragdollPoseInterpolation) {
        std::wstring hookError;
        const runtime::Options options{
            loaded.settings.features.renderUnlock,
            config::AnySmoothingFeatureEnabled(loaded.settings.features),
            loaded.settings.features.cameraInterpolation,
            loaded.settings.features.handsRootInterpolation,
            loaded.settings.features.handsPoseInterpolation,
            loaded.settings.features.worldRootInterpolation,
            loaded.settings.features.worldPoseInterpolation,
            loaded.settings.features.agentRootInterpolation,
            loaded.settings.features.agentPoseInterpolation,
            loaded.settings.features.guiMouseOverlay,
            loaded.settings.features.mouseOverlay,
            loaded.settings.features.ragdollPoseInterpolation,
            loaded.settings.reportIntervalSeconds,
            loaded.settings.detailedReports,
            loaded.settings.healthReports};
        if (!runtimeMappings.ok ||
            !runtime::Initialize(runtimeMappings.mappings, options, hookError)) {
            if (!hookError.empty()) {
                log::Write(log::Level::error,
                           L"Runtime-hook initialization failed: " + hookError);
            }
            log::Write(log::Level::error,
                       L"Fail-closed: no engine hooks were left installed.");
            g_state.store(BootstrapState::rejected, std::memory_order_release);
            return 6;
        }
        log::Write(log::Level::info,
                   L"Requested runtime hooks are installed.");
        g_state.store(BootstrapState::readyWithHooks,
                      std::memory_order_release);
        return 0;
    }

    log::Write(log::Level::info,
               L"No runtime work requested; no hooks installed.");
    g_state.store(BootstrapState::readyWithoutHooks, std::memory_order_release);
    return 0;
}

} // namespace soma

