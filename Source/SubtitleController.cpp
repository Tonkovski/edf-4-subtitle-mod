#include "SubtitleController.h"

#include "Log.h"

#include <algorithm>

SubtitleController::SubtitleController(
    const SubtitleDatabase& database,
    const ModConfig& config)
    : database_(database),
      config_(config) {
    active_.reserve(config.maximumSubtitles);
}

void SubtitleController::Enqueue(VoiceKind kind, std::string cue) {
    if (cue.empty()) {
        return;
    }

    std::scoped_lock lock(pendingMutex_);
    if (pending_.size() >= MaximumPendingVoices) {
        pending_.pop_front();
    }
    pending_.push_back(PendingVoice{kind, std::move(cue)});
}

std::vector<RenderableSubtitle> SubtitleController::Update(
    std::chrono::steady_clock::time_point now) {
    std::deque<PendingVoice> received;
    {
        std::scoped_lock lock(pendingMutex_);
        received.swap(pending_);
    }

    for (auto& event : received) {
        Activate(std::move(event), now);
    }

    std::erase_if(active_, [now](const ActiveSubtitle& subtitle) {
        return now >= subtitle.expires;
    });

    std::vector<RenderableSubtitle> result;
    result.reserve(active_.size());
    for (const auto& subtitle : active_) {
        result.push_back(RenderableSubtitle{
            subtitle.kind,
            subtitle.text,
            Opacity(subtitle, now),
        });
    }
    return result;
}

void SubtitleController::Activate(
    PendingVoice event,
    std::chrono::steady_clock::time_point now) {
    auto resolved = database_.Resolve(event.cue);
    if (!resolved) {
        if (config_.debugVoiceEvents && loggedMissingCues_.insert(event.cue).second) {
            logging::Warning("Missing subtitle for cue " + event.cue + ".");
        }
        return;
    }

    if (config_.debugVoiceEvents) {
        logging::Info(
            "Cue " + event.cue + " -> " + resolved->cue + " [" +
            std::to_string(resolved->displayMilliseconds) + " ms].");
    }

    if (active_.size() >= config_.maximumSubtitles) {
        const auto eviction = std::min_element(
            active_.begin(),
            active_.end(),
            [](const ActiveSubtitle& left, const ActiveSubtitle& right) {
                return left.expires < right.expires;
            });
        if (eviction != active_.end()) {
            active_.erase(eviction);
        }
    }

    const auto fadeOutStarted =
        now + std::chrono::milliseconds(resolved->displayMilliseconds);
    active_.push_back(ActiveSubtitle{
        event.kind,
        std::move(resolved->cue),
        std::move(resolved->text),
        now,
        fadeOutStarted,
        fadeOutStarted + std::chrono::milliseconds(config_.fadeOutMs),
    });
}

float SubtitleController::Opacity(
    const ActiveSubtitle& subtitle,
    std::chrono::steady_clock::time_point now) const {
    float opacity = 1.0F;

    if (config_.fadeInMs != 0) {
        const auto fadeInElapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - subtitle.started).count();
        opacity = std::min(
            opacity,
            std::clamp(
                static_cast<float>(fadeInElapsed) / static_cast<float>(config_.fadeInMs),
                0.0F,
                1.0F));
    }

    if (config_.fadeOutMs != 0 && now >= subtitle.fadeOutStarted) {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(subtitle.expires - now).count();
        opacity = std::min(
            opacity,
            std::clamp(
                static_cast<float>(remaining) / static_cast<float>(config_.fadeOutMs),
                0.0F,
                1.0F));
    }

    return opacity;
}

