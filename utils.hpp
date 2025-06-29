#pragma once

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include <absl/strings/charconv.h>

inline std::optional<std::string> findToken(const std::filesystem::path& rootDir) noexcept {
    std::string token;
    std::ifstream tokenFile(rootDir / "token");
    if (!tokenFile.is_open()) {
        return {};
    }
    tokenFile >> token;

    return token;
}

inline std::optional<double> strToDouble(std::string_view str) {
    double result;
    if (absl::from_chars(str.begin(), str.end(), result).ec != std::errc()) {
        return {};
    }
    return result;
}
