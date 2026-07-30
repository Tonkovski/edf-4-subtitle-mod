#pragma once

#include "Config.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

enum class VoiceKind {
    Event,
    Ambient,
};

struct ResolvedSubtitle {
    std::string cue;
    std::string text;
    std::uint32_t displayMilliseconds;
};

class SubtitleDatabase {
public:
    bool Load(const ModConfig& config);
    [[nodiscard]] std::optional<ResolvedSubtitle> Resolve(std::string_view rawCue) const;
    [[nodiscard]] const std::vector<std::string>& GlyphSource() const;
    [[nodiscard]] static std::string NormalizeCue(std::string_view cue);

private:
    std::unordered_map<std::string, std::string> subtitles_;
    std::unordered_map<std::string, std::uint32_t> durations_;
    std::vector<std::string> glyphSource_;
    const ModConfig* config_ = nullptr;
};

