#pragma once

#include "Config.h"
#include "SubtitleController.h"

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>

class GameHooks {
public:
    bool Install(HMODULE gameModule, const ModConfig& config, SubtitleController& controller);
    void Remove();

private:
    // Verified against EDF41.exe call sites. The floating-point parameter
    // positions are ABI-significant on Windows x64.
    using EventVoiceFunction = bool(__fastcall*)(
        void* context,
        const char* cue,
        float argument3,
        float argument4,
        void* argument5);
    using AmbientVoiceFunction = bool(__fastcall*)(
        void* context,
        const char* cue,
        void* argument3,
        float argument4,
        float argument5,
        void* argument6);

    static bool __fastcall EventVoiceHook(
        void* context,
        const char* cue,
        float argument3,
        float argument4,
        void* argument5);
    static bool __fastcall AmbientVoiceHook(
        void* context,
        const char* cue,
        void* argument3,
        float argument4,
        float argument5,
        void* argument6);

    static std::optional<std::string> CopyCue(const char* cue);
    static bool IsExecutableAddress(const void* address);
    static std::string DescribeAddress(const void* address);

    static GameHooks* instance_;

    SubtitleController* controller_ = nullptr;
    EventVoiceFunction originalEventVoice_ = nullptr;
    AmbientVoiceFunction originalAmbientVoice_ = nullptr;
    bool installed_ = false;
};
