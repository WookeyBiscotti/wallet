#pragma once

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

inline std::optional<std::string> findToken(const std::filesystem::path& rootDir) noexcept {
    std::string token;
    std::ifstream tokenFile(rootDir / "token");
    if (!tokenFile.is_open()) {
        return {};
    }
    tokenFile >> token;

    return token;
}
