#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace logging {

enum class Level {
    Info,
    Warning,
    Error,
};

void Initialize(const std::filesystem::path& path);
void Write(Level level, std::string_view message);
void Info(std::string_view message);
void Warning(std::string_view message);
void Error(std::string_view message);
std::string PathToUtf8(const std::filesystem::path& path);

} // namespace logging

