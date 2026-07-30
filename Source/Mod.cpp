#include "Config.h"
#include "GameHooks.h"
#include "Log.h"
#include "PluginAPI.h"
#include "Renderer.h"
#include "SubtitleController.h"
#include "SubtitleDatabase.h"

#include <windows.h>

#include <filesystem>
#include <memory>
#include <sstream>
#include <string>

namespace {

int g_moduleAnchor = 0;

std::filesystem::path PluginDirectory() {
    HMODULE module = nullptr;
    const DWORD flags =
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (GetModuleHandleExW(
            flags,
            reinterpret_cast<LPCWSTR>(&g_moduleAnchor),
            &module) == FALSE) {
        return std::filesystem::current_path();
    }

    std::wstring buffer(32768, L'\0');
    const DWORD length =
        GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return std::filesystem::current_path();
    }
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

std::string DescribeGameImage(HMODULE module) {
    const auto* base = reinterpret_cast<const unsigned char*>(module);
    const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        return "invalid PE image";
    }

    const auto* ntHeaders =
        reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
        return "invalid PE headers";
    }

    std::ostringstream output;
    output << "base=0x" << std::hex << std::uppercase
           << reinterpret_cast<std::uintptr_t>(module) << ", image_size=0x"
           << ntHeaders->OptionalHeader.SizeOfImage << ", timestamp=0x"
           << ntHeaders->FileHeader.TimeDateStamp;
    return output.str();
}

class Runtime {
public:
    bool Initialize(const std::filesystem::path& pluginDirectory, HMODULE gameModule) {
        config_ = ModConfig::Load(pluginDirectory / L"subtitles.toml", pluginDirectory);
        if (!database_.Load(config_)) {
            return false;
        }

        controller_ = std::make_unique<SubtitleController>(database_, config_);
        if (!renderer_.InstallHooks(config_, database_, *controller_)) {
            controller_.reset();
            return false;
        }

        if (!gameHooks_.Install(gameModule, config_, *controller_)) {
            renderer_.RemoveHooks();
            controller_.reset();
            return false;
        }

        return true;
    }

private:
    ModConfig config_;
    SubtitleDatabase database_;
    std::unique_ptr<SubtitleController> controller_;
    Renderer renderer_;
    GameHooks gameHooks_;
};

std::unique_ptr<Runtime> g_runtime;

} // namespace

extern "C" __declspec(dllexport) bool __fastcall EML4_Load(PluginInfo* information) {
    if (information == nullptr) {
        return false;
    }

    information->infoVersion = PluginInfo::MaxInfoVer;
    information->name = "EDF 4.1 Native Subtitles";
    information->version = PLUG_VER(0, 1, 1, 0);

    try {
        const auto pluginDirectory = PluginDirectory();
        logging::Initialize(pluginDirectory / L"edf41_subtitles.log");
        logging::Info("Loading EDF 4.1 native subtitles 0.1.1.");
        logging::Info("Plugin directory: " + logging::PathToUtf8(pluginDirectory) + ".");

        HMODULE gameModule = GetModuleHandleW(L"EDF41.exe");
        if (gameModule == nullptr) {
            logging::Error("EDF41.exe is not the current process.");
            return false;
        }
        logging::Info("EDF41.exe " + DescribeGameImage(gameModule) + ".");

        auto runtime = std::make_unique<Runtime>();
        if (!runtime->Initialize(pluginDirectory, gameModule)) {
            logging::Error("Plugin initialization failed; the DLL will be unloaded.");
            return false;
        }

        g_runtime = std::move(runtime);
        logging::Info("EDF 4.1 native subtitles initialized successfully.");
        return true;
    } catch (const std::exception& exception) {
        logging::Error(std::string("Plugin initialization threw an exception: ") + exception.what());
        return false;
    } catch (...) {
        logging::Error("Plugin initialization threw an unknown exception.");
        return false;
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
