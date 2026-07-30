#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

struct ModConfig {
    std::filesystem::path subtitleFile;
    std::filesystem::path durationFile;
    std::filesystem::path fontFile;

    float fontSize = 24.0F;

    std::uint32_t fallbackDisplayMs = 4500;
    std::uint32_t minimumDisplayMs = 2200;
    std::uint32_t tailPaddingMs = 450;
    std::uint32_t fadeInMs = 140;
    std::uint32_t fadeOutMs = 220;
    std::size_t maximumSubtitles = 3;

    float maximumWidth = 1200.0F;
    float marginBottom = 110.0F;
    float paddingX = 18.0F;
    float paddingY = 14.0F;
    float lineGap = 8.0F;

    std::uint32_t backgroundColor = 0x101218;
    float backgroundAlpha = 0.72F;
    std::uint32_t eventColor = 0xFFF0A6;
    std::uint32_t ambientColor = 0x8FE7FF;

    std::uintptr_t eventVoiceRva = 0x3E46E0;
    std::uintptr_t ambientVoiceRva = 0x3E47E0;
    bool debugVoiceEvents = false;

    static ModConfig Load(
        const std::filesystem::path& configPath,
        const std::filesystem::path& pluginDirectory);
};
