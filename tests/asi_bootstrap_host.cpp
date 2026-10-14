#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
}

} // namespace

int wmain(const int argc, wchar_t** argv) {
    if (argc != 2) {
        std::wcerr << L"usage: asi_bootstrap_host <plugin>\n";
        return 2;
    }

    wchar_t tempRoot[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, tempRoot) == 0) {
        std::wcerr << L"GetTempPathW failed\n";
        return 3;
    }
    const std::filesystem::path directory =
        std::filesystem::path(tempRoot) /
        (L"SOMA.FPSFix-bootstrap-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        std::wcerr << L"could not create temporary directory\n";
        return 4;
    }

    const auto plugin = directory / L"SOMA.FPSFix.asi";
    const auto ini = directory / L"SOMA.FPSFix.ini";
    const auto log = directory / L"SOMA.FPSFix.log";
    std::filesystem::copy_file(argv[1], plugin,
                               std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        std::wcerr << L"could not copy plugin\n";
        return 5;
    }
    {
        std::ofstream stream(ini, std::ios::binary);
        stream << "[General]\nEnabled=true\nDiagnostics=true\n"
                  "[Logging]\nHealthReports=false\n"
                  "ReportIntervalSeconds=5\n"
                  "[Features]\nRenderUnlock=true\n"
                  "[Timing]\nTargetFPS=0\n";
    }

    const HMODULE module = LoadLibraryW(plugin.c_str());
    if (module == nullptr) {
        std::wcerr << L"LoadLibraryW failed: " << GetLastError() << L'\n';
        return 6;
    }

    bool rejected = false;
    for (int attempt = 0; attempt < 200; ++attempt) {
        const std::string text = ReadAll(log);
        if (text.find("SOMA runtime mapping rejected") != std::string::npos &&
            text.find("Fail-closed: no engine hooks were installed") !=
                std::string::npos) {
            rejected = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    (void)FreeLibrary(module);

    std::filesystem::remove(plugin, error);
    std::filesystem::remove(ini, error);
    std::filesystem::remove(log, error);
    std::filesystem::remove(directory, error);

    if (!rejected) {
        std::cerr << "plugin did not log incompatible-host mapping rejection\n";
        return 1;
    }
    std::cout << "incompatible scaffold host rejected with no hooks\n";
    return 0;
}

