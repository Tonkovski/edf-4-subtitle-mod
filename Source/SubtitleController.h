#pragma once

#include "Config.h"
#include "SubtitleDatabase.h"

#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

struct RenderableSubtitle {
    VoiceKind kind;
    std::string text;
    float opacity;
};

class SubtitleController {
public:
    SubtitleController(const SubtitleDatabase& database, const ModConfig& config);

    void Enqueue(VoiceKind kind, std::string cue);
    [[nodiscard]] std::vector<RenderableSubtitle> Update(
        std::chrono::steady_clock::time_point now);

private:
    struct PendingVoice {
        VoiceKind kind;
        std::string cue;
    };

    struct ActiveSubtitle {
        VoiceKind kind;
        std::string cue;
        std::string text;
        std::chrono::steady_clock::time_point started;
        std::chrono::steady_clock::time_point fadeOutStarted;
        std::chrono::steady_clock::time_point expires;
    };

    void Activate(PendingVoice event, std::chrono::steady_clock::time_point now);
    [[nodiscard]] float Opacity(
        const ActiveSubtitle& subtitle,
        std::chrono::steady_clock::time_point now) const;

    static constexpr std::size_t MaximumPendingVoices = 256;

    const SubtitleDatabase& database_;
    const ModConfig& config_;
    std::mutex pendingMutex_;
    std::deque<PendingVoice> pending_;
    std::vector<ActiveSubtitle> active_;
    std::unordered_set<std::string> loggedMissingCues_;
};

