#include "Config.h"
#include "SubtitleController.h"
#include "SubtitleDatabase.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

bool Check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    bool passed = true;
    const std::filesystem::path sourceDirectory = EDF41_SOURCE_DIR;

    ModConfig config;
    config.subtitleFile = sourceDirectory / L"subtitles.txt";
    config.durationFile = sourceDirectory / L"cuelength.txt";
    config.minimumDisplayMs = 2200;
    config.tailPaddingMs = 450;
    config.fallbackDisplayMs = 4500;
    config.fadeInMs = 100;
    config.fadeOutMs = 200;
    config.maximumSubtitles = 3;

    SubtitleDatabase database;
    passed &= Check(database.Load(config), "database loads the repository data");
    passed &= Check(
        SubtitleDatabase::NormalizeCue("A001AA_E") == "A001AA",
        "_E suffix is normalized");
    passed &= Check(
        SubtitleDatabase::NormalizeCue("A001AA_S") == "A001AA",
        "_S suffix is normalized");
    passed &= Check(
        SubtitleDatabase::NormalizeCue("A001AA") == "A001AA",
        "ordinary cue is unchanged");

    const auto resolved = database.Resolve("A001AA");
    passed &= Check(resolved.has_value(), "known cue resolves");
    if (resolved) {
        passed &= Check(!resolved->text.empty(), "known cue has subtitle text");
        passed &= Check(
            resolved->displayMilliseconds == 2200,
            "minimum display duration is applied");
    }

    const auto suffixed = database.Resolve("A001AA_E");
    passed &= Check(suffixed.has_value(), "suffixed cue resolves through normalized key");
    passed &= Check(!database.Resolve("THIS_CUE_DOES_NOT_EXIST"), "missing cue does not resolve");

    const auto now = std::chrono::steady_clock::now();
    SubtitleController controller(database, config);
    controller.Enqueue(VoiceKind::Event, "A001AA");
    auto rendered = controller.Update(now);
    passed &= Check(rendered.size() == 1, "queued subtitle becomes active");
    if (!rendered.empty()) {
        passed &= Check(rendered.front().kind == VoiceKind::Event, "voice category is preserved");
        passed &= Check(rendered.front().opacity == 0.0F, "fade starts transparent");
    }

    rendered = controller.Update(now + std::chrono::milliseconds(100));
    passed &= Check(
        !rendered.empty() && rendered.front().opacity == 1.0F,
        "fade reaches full opacity by configured duration");

    SubtitleController stackController(database, config);
    stackController.Enqueue(VoiceKind::Event, "A001AA");
    stackController.Enqueue(VoiceKind::Event, "A001AB");
    stackController.Enqueue(VoiceKind::Ambient, "A001AC");
    stackController.Enqueue(VoiceKind::Ambient, "A001AD");
    rendered = stackController.Update(now);
    passed &= Check(rendered.size() == 3, "subtitle stack obeys configured maximum");

    if (!passed) {
        return 1;
    }
    std::cout << "[OK] subtitle core tests passed\n";
    return 0;
}

