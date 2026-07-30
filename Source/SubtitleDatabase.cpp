#include "SubtitleDatabase.h"

#include "Log.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>

namespace {

void RemoveBom(std::string& value) {
    constexpr char bom[] = {'\xEF', '\xBB', '\xBF'};
    if (value.size() >= 3 && std::equal(std::begin(bom), std::end(bom), value.begin())) {
        value.erase(0, 3);
    }
}

void RemoveCarriageReturn(std::string& value) {
    if (!value.empty() && value.back() == '\r') {
        value.pop_back();
    }
}

} // namespace

bool SubtitleDatabase::Load(const ModConfig& config) {
    config_ = &config;
    subtitles_.clear();
    durations_.clear();
    glyphSource_.clear();

    std::ifstream subtitleFile(config.subtitleFile);
    if (!subtitleFile) {
        logging::Error(
            "Failed to open subtitle file " + logging::PathToUtf8(config.subtitleFile) + ".");
        return false;
    }

    std::size_t malformedSubtitles = 0;
    std::size_t duplicateSubtitles = 0;
    std::string line;
    bool firstLine = true;
    while (std::getline(subtitleFile, line)) {
        RemoveCarriageReturn(line);
        if (firstLine) {
            RemoveBom(line);
            firstLine = false;
        }

        const auto separator = line.find('=');
        if (separator == std::string::npos || separator == 0) {
            ++malformedSubtitles;
            continue;
        }

        std::string cue = line.substr(0, separator);
        std::string text = line.substr(separator + 1);
        if (text.empty()) {
            continue;
        }

        const auto [iterator, inserted] =
            subtitles_.insert_or_assign(std::move(cue), std::move(text));
        if (!inserted) {
            ++duplicateSubtitles;
        }
        (void)iterator;
    }

    std::ifstream durationFile(config.durationFile);
    if (!durationFile) {
        logging::Warning(
            "Duration file not found at " + logging::PathToUtf8(config.durationFile) +
            "; fallback timing will be used.");
    } else {
        std::size_t malformedDurations = 0;
        firstLine = true;
        while (std::getline(durationFile, line)) {
            RemoveCarriageReturn(line);
            if (firstLine) {
                RemoveBom(line);
                firstLine = false;
            }

            const auto separator = line.find('=');
            if (separator == std::string::npos || separator == 0) {
                ++malformedDurations;
                continue;
            }

            std::uint32_t duration = 0;
            const std::string_view durationText(line.data() + separator + 1, line.size() - separator - 1);
            const auto parseResult =
                std::from_chars(durationText.data(), durationText.data() + durationText.size(), duration);
            if (parseResult.ec != std::errc{} || parseResult.ptr != durationText.data() + durationText.size()) {
                ++malformedDurations;
                continue;
            }

            durations_.insert_or_assign(line.substr(0, separator), duration);
        }

        if (malformedDurations != 0) {
            logging::Warning(
                "Ignored " + std::to_string(malformedDurations) + " malformed duration records.");
        }
    }

    glyphSource_.reserve(subtitles_.size());
    for (const auto& [cue, text] : subtitles_) {
        (void)cue;
        glyphSource_.push_back(text);
    }

    logging::Info(
        "Loaded " + std::to_string(subtitles_.size()) + " subtitles and " +
        std::to_string(durations_.size()) + " cue durations.");
    if (malformedSubtitles != 0 || duplicateSubtitles != 0) {
        logging::Warning(
            "Subtitle data contained " + std::to_string(malformedSubtitles) +
            " malformed and " + std::to_string(duplicateSubtitles) + " duplicate records.");
    }
    return !subtitles_.empty();
}

std::optional<ResolvedSubtitle> SubtitleDatabase::Resolve(std::string_view rawCue) const {
    if (config_ == nullptr || rawCue.empty()) {
        return std::nullopt;
    }

    const std::string normalized = NormalizeCue(rawCue);
    const auto subtitle = subtitles_.find(normalized);
    if (subtitle == subtitles_.end()) {
        return std::nullopt;
    }

    std::uint32_t displayMilliseconds = config_->fallbackDisplayMs;
    auto duration = durations_.find(std::string(rawCue));
    if (duration == durations_.end()) {
        duration = durations_.find(normalized);
    }
    if (duration != durations_.end()) {
        const std::uint64_t padded =
            static_cast<std::uint64_t>(duration->second) + config_->tailPaddingMs;
        displayMilliseconds = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(padded, std::numeric_limits<std::uint32_t>::max()));
        displayMilliseconds = std::max(displayMilliseconds, config_->minimumDisplayMs);
    }

    return ResolvedSubtitle{normalized, subtitle->second, displayMilliseconds};
}

const std::vector<std::string>& SubtitleDatabase::GlyphSource() const {
    return glyphSource_;
}

std::string SubtitleDatabase::NormalizeCue(std::string_view cue) {
    if (cue.size() >= 2 && cue[cue.size() - 2] == '_' &&
        (cue.back() == 'E' || cue.back() == 'S')) {
        cue.remove_suffix(2);
    }
    return std::string(cue);
}

