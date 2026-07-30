#include "GameHooks.h"

#include "Log.h"

#include <detours.h>
#include <psapi.h>

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>

GameHooks* GameHooks::instance_ = nullptr;

namespace {

bool IsReadableProtection(DWORD protection) {
    if ((protection & PAGE_GUARD) != 0 || (protection & PAGE_NOACCESS) != 0) {
        return false;
    }
    const DWORD baseProtection = protection & 0xFFU;
    return baseProtection == PAGE_READONLY || baseProtection == PAGE_READWRITE ||
        baseProtection == PAGE_WRITECOPY || baseProtection == PAGE_EXECUTE_READ ||
        baseProtection == PAGE_EXECUTE_READWRITE || baseProtection == PAGE_EXECUTE_WRITECOPY;
}

bool ImageContainsRva(HMODULE module, std::uintptr_t rva) {
    MODULEINFO information{};
    if (GetModuleInformation(
            GetCurrentProcess(),
            module,
            &information,
            static_cast<DWORD>(sizeof(information))) == FALSE) {
        return false;
    }
    return rva < static_cast<std::uintptr_t>(information.SizeOfImage);
}

} // namespace

bool GameHooks::Install(
    HMODULE gameModule,
    const ModConfig& config,
    SubtitleController& controller) {
    if (installed_ || gameModule == nullptr) {
        return false;
    }
    if (!ImageContainsRva(gameModule, config.eventVoiceRva) ||
        !ImageContainsRva(gameModule, config.ambientVoiceRva)) {
        logging::Error("Configured voice RVA lies outside EDF41.exe.");
        return false;
    }

    const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
    originalEventVoice_ =
        reinterpret_cast<EventVoiceFunction>(base + config.eventVoiceRva);
    originalAmbientVoice_ =
        reinterpret_cast<AmbientVoiceFunction>(base + config.ambientVoiceRva);

    if (!IsExecutableAddress(reinterpret_cast<const void*>(originalEventVoice_)) ||
        !IsExecutableAddress(reinterpret_cast<const void*>(originalAmbientVoice_))) {
        logging::Error("One or both configured voice RVAs are not executable memory.");
        return false;
    }

    logging::Info(
        "Event voice target: " +
        DescribeAddress(reinterpret_cast<const void*>(originalEventVoice_)) + ".");
    logging::Info(
        "Ambient voice target: " +
        DescribeAddress(reinterpret_cast<const void*>(originalAmbientVoice_)) + ".");

    controller_ = &controller;
    instance_ = this;

    LONG result = DetourTransactionBegin();
    if (result != NO_ERROR) {
        logging::Error("DetourTransactionBegin failed for voice hooks: " + std::to_string(result));
        instance_ = nullptr;
        controller_ = nullptr;
        return false;
    }

    DetourUpdateThread(GetCurrentThread());
    result = DetourAttach(
        reinterpret_cast<PVOID*>(&originalEventVoice_),
        reinterpret_cast<PVOID>(EventVoiceHook));
    if (result == NO_ERROR) {
        result = DetourAttach(
            reinterpret_cast<PVOID*>(&originalAmbientVoice_),
            reinterpret_cast<PVOID>(AmbientVoiceHook));
    }

    if (result != NO_ERROR) {
        DetourTransactionAbort();
        logging::Error("DetourAttach failed for voice hooks: " + std::to_string(result));
        instance_ = nullptr;
        controller_ = nullptr;
        return false;
    }

    result = DetourTransactionCommit();
    if (result != NO_ERROR) {
        logging::Error("DetourTransactionCommit failed for voice hooks: " + std::to_string(result));
        instance_ = nullptr;
        controller_ = nullptr;
        return false;
    }

    installed_ = true;
    logging::Info("EDF41 voice hooks installed.");
    return true;
}

void GameHooks::Remove() {
    if (!installed_) {
        return;
    }

    if (DetourTransactionBegin() == NO_ERROR) {
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(
            reinterpret_cast<PVOID*>(&originalEventVoice_),
            reinterpret_cast<PVOID>(EventVoiceHook));
        DetourDetach(
            reinterpret_cast<PVOID*>(&originalAmbientVoice_),
            reinterpret_cast<PVOID>(AmbientVoiceHook));
        DetourTransactionCommit();
    }

    installed_ = false;
    controller_ = nullptr;
    instance_ = nullptr;
}

bool __fastcall GameHooks::EventVoiceHook(
    void* context,
    const char* cue,
    float argument3,
    float argument4,
    void* argument5) {
    if (instance_ != nullptr && instance_->controller_ != nullptr) {
        if (auto copiedCue = CopyCue(cue)) {
            instance_->controller_->Enqueue(VoiceKind::Event, std::move(*copiedCue));
        }
    }
    return instance_->originalEventVoice_(
        context,
        cue,
        argument3,
        argument4,
        argument5);
}

bool __fastcall GameHooks::AmbientVoiceHook(
    void* context,
    const char* cue,
    void* argument3,
    float argument4,
    float argument5,
    void* argument6) {
    if (instance_ != nullptr && instance_->controller_ != nullptr) {
        if (auto copiedCue = CopyCue(cue)) {
            instance_->controller_->Enqueue(VoiceKind::Ambient, std::move(*copiedCue));
        }
    }
    return instance_->originalAmbientVoice_(
        context,
        cue,
        argument3,
        argument4,
        argument5,
        argument6);
}

std::optional<std::string> GameHooks::CopyCue(const char* cue) {
    constexpr std::size_t maximumCueLength = 255;
    if (cue == nullptr) {
        return std::nullopt;
    }

    MEMORY_BASIC_INFORMATION information{};
    if (VirtualQuery(cue, &information, sizeof(information)) == 0 ||
        information.State != MEM_COMMIT || !IsReadableProtection(information.Protect)) {
        return std::nullopt;
    }

    const auto address = reinterpret_cast<std::uintptr_t>(cue);
    const auto regionEnd = reinterpret_cast<std::uintptr_t>(information.BaseAddress) +
        information.RegionSize;
    if (regionEnd <= address) {
        return std::nullopt;
    }

    const std::size_t available =
        std::min<std::size_t>(maximumCueLength + 1, regionEnd - address);
    const auto terminator = std::find(cue, cue + available, '\0');
    if (terminator == cue + available || terminator == cue) {
        return std::nullopt;
    }

    return std::string(cue, terminator);
}

bool GameHooks::IsExecutableAddress(const void* address) {
    MEMORY_BASIC_INFORMATION information{};
    if (VirtualQuery(address, &information, sizeof(information)) == 0 ||
        information.State != MEM_COMMIT || (information.Protect & PAGE_GUARD) != 0) {
        return false;
    }

    const DWORD protection = information.Protect & 0xFFU;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

std::string GameHooks::DescribeAddress(const void* address) {
    constexpr std::size_t byteCount = 12;
    const auto* bytes = static_cast<const unsigned char*>(address);
    std::ostringstream output;
    output << "0x" << std::hex << std::uppercase << reinterpret_cast<std::uintptr_t>(address)
           << " [";
    for (std::size_t index = 0; index < byteCount; ++index) {
        if (index != 0) {
            output << ' ';
        }
        output << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(bytes[index]);
    }
    output << ']';
    return output.str();
}
