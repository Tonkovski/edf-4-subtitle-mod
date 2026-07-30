#include "Config.h"

#include "Log.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>

namespace {

std::string Trim(std::string_view value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) {
        return std::isspace(character) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) {
                          return std::isspace(character) != 0;
                      }).base();
    if (first >= last) {
        return {};
    }
    return {first, last};
}

std::string StripComment(std::string_view line) {
    bool quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        if (line[index] == '"' && (index == 0 || line[index - 1] != '\\')) {
            quoted = !quoted;
        } else if (line[index] == '#' && !quoted) {
            return std::string(line.substr(0, index));
        }
    }
    return std::string(line);
}

std::string Unquote(std::string value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

bool ParseBool(std::string value, bool fallback) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (value == "true" || value == "1" || value == "yes") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no") {
        return false;
    }
    return fallback;
}

template <typename Number>
Number ParseUnsigned(const std::string& value, Number fallback) {
    try {
        const auto parsed = std::stoull(value, nullptr, 0);
        if (parsed > static_cast<unsigned long long>(std::numeric_limits<Number>::max())) {
            return fallback;
        }
        return static_cast<Number>(parsed);
    } catch (...) {
        return fallback;
    }
}

float ParseFloat(const std::string& value, float fallback) {
    try {
        return std::stof(value);
    } catch (...) {
        return fallback;
    }
}

std::uint32_t ParseColor(std::string value, std::uint32_t fallback) {
    value = Unquote(std::move(value));
    if (!value.empty() && value.front() == '#') {
        value.erase(value.begin());
    }
    if (value.size() != 6) {
        return fallback;
    }
    try {
        return static_cast<std::uint32_t>(std::stoul(value, nullptr, 16));
    } catch (...) {
        return fallback;
    }
}

std::filesystem::path ResolvePath(
    const std::string& configured,
    const std::filesystem::path& pluginDirectory) {
    if (configured.empty()) {
        return {};
    }

    const std::u8string utf8Path(
        reinterpret_cast<const char8_t*>(configured.data()),
        reinterpret_cast<const char8_t*>(configured.data() + configured.size()));
    std::filesystem::path path(utf8Path);
    if (path.is_relative()) {
        path = pluginDirectory / path;
    }
    return path.lexically_normal();
}

} // namespace

ModConfig ModConfig::Load(
    const std::filesystem::path& configPath,
    const std::filesystem::path& pluginDirectory) {
    ModConfig result;
    result.subtitleFile = pluginDirectory / L"subtitles.txt";
    result.durationFile = pluginDirectory / L"cuelength.txt";

    std::ifstream file(configPath);
    if (!file) {
        logging::Warning(
            "Configuration not found at " + logging::PathToUtf8(configPath) +
            "; using defaults.");
        return result;
    }

    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        line = Trim(StripComment(line));
        if (line.empty()) {
            continue;
        }

        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            logging::Warning(
                "Ignoring malformed configuration line " + std::to_string(lineNumber) + ".");
            continue;
        }

        const std::string key = Trim(std::string_view(line).substr(0, separator));
        const std::string value = Trim(std::string_view(line).substr(separator + 1));

        if (key == "subtitle_file") {
            result.subtitleFile = ResolvePath(Unquote(value), pluginDirectory);
        } else if (key == "duration_file") {
            result.durationFile = ResolvePath(Unquote(value), pluginDirectory);
        } else if (key == "font_path") {
            result.fontFile = ResolvePath(Unquote(value), pluginDirectory);
        } else if (key == "font_size") {
            result.fontSize = ParseFloat(value, result.fontSize);
        } else if (key == "fallback_display_ms") {
            result.fallbackDisplayMs = ParseUnsigned(value, result.fallbackDisplayMs);
        } else if (key == "minimum_display_ms") {
            result.minimumDisplayMs = ParseUnsigned(value, result.minimumDisplayMs);
        } else if (key == "tail_padding_ms") {
            result.tailPaddingMs = ParseUnsigned(value, result.tailPaddingMs);
        } else if (key == "fade_in_ms") {
            result.fadeInMs = ParseUnsigned(value, result.fadeInMs);
        } else if (key == "fade_out_ms") {
            result.fadeOutMs = ParseUnsigned(value, result.fadeOutMs);
        } else if (key == "maximum_subtitles") {
            result.maximumSubtitles = ParseUnsigned(value, result.maximumSubtitles);
        } else if (key == "maximum_width") {
            result.maximumWidth = ParseFloat(value, result.maximumWidth);
        } else if (key == "margin_bottom") {
            result.marginBottom = ParseFloat(value, result.marginBottom);
        } else if (key == "padding_x") {
            result.paddingX = ParseFloat(value, result.paddingX);
        } else if (key == "padding_y") {
            result.paddingY = ParseFloat(value, result.paddingY);
        } else if (key == "line_gap") {
            result.lineGap = ParseFloat(value, result.lineGap);
        } else if (key == "background_color") {
            result.backgroundColor = ParseColor(value, result.backgroundColor);
        } else if (key == "background_alpha") {
            result.backgroundAlpha = ParseFloat(value, result.backgroundAlpha);
        } else if (key == "event_color") {
            result.eventColor = ParseColor(value, result.eventColor);
        } else if (key == "ambient_color") {
            result.ambientColor = ParseColor(value, result.ambientColor);
        } else if (key == "event_voice_rva") {
            result.eventVoiceRva = ParseUnsigned(value, result.eventVoiceRva);
        } else if (key == "ambient_voice_rva") {
            result.ambientVoiceRva = ParseUnsigned(value, result.ambientVoiceRva);
        } else if (key == "debug_voice_events") {
            result.debugVoiceEvents = ParseBool(value, result.debugVoiceEvents);
        }
    }

    result.fontSize = std::clamp(result.fontSize, 8.0F, 96.0F);
    result.backgroundAlpha = std::clamp(result.backgroundAlpha, 0.0F, 1.0F);
    result.maximumWidth = std::max(result.maximumWidth, 240.0F);
    result.maximumSubtitles = std::clamp<std::size_t>(result.maximumSubtitles, 1, 10);

    logging::Info("Loaded configuration from " + logging::PathToUtf8(configPath) + ".");
    return result;
}
