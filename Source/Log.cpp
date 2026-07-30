#include "Log.h"

#include <windows.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace logging {
namespace {

std::mutex g_logMutex;
std::filesystem::path g_logPath;

const char* LevelName(Level level) {
    switch (level) {
    case Level::Info:
        return "INFO";
    case Level::Warning:
        return "WARN";
    case Level::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}

std::string Timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &time);

    std::ostringstream out;
    out << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

} // namespace

void Initialize(const std::filesystem::path& path) {
    std::scoped_lock lock(g_logMutex);
    g_logPath = path;

    std::ofstream file(g_logPath, std::ios::out | std::ios::trunc);
    if (file) {
        file << "EDF 4.1 native subtitles log\n";
    }
}

void Write(Level level, std::string_view message) {
    const std::string line =
        "[" + Timestamp() + "] [" + LevelName(level) + "] " + std::string(message) + "\n";

    std::scoped_lock lock(g_logMutex);
    OutputDebugStringA(line.c_str());
    if (!g_logPath.empty()) {
        std::ofstream file(g_logPath, std::ios::out | std::ios::app);
        if (file) {
            file << line;
        }
    }
}

void Info(std::string_view message) {
    Write(Level::Info, message);
}

void Warning(std::string_view message) {
    Write(Level::Warning, message);
}

void Error(std::string_view message) {
    Write(Level::Error, message);
}

std::string PathToUtf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

} // namespace logging

