#pragma once

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include <absl/strings/charconv.h>
#include <absl/time/time.h>

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

inline std::optional<int> strToInt(std::string_view str) {
    int result;
    if (std::from_chars(str.begin(), str.end(), result).ec != std::errc()) {
        return {};
    }
    return result;
}

template<class T>
inline std::optional<int> strToT(std::string_view str) {
    T result;
    if (std::from_chars(str.begin(), str.end(), result).ec != std::errc()) {
        return {};
    }

    return result;
}

inline std::string formatWithApostrophes(std::int64_t num) {
    std::string s = std::to_string(num);
    int n = s.length() - 3;
    while (n > 0) {
        s.insert(n, "'");
        n -= 3;
    }
    return s;
}

inline absl::TimeZone getTimeZone(std::string_view str) {
    absl::TimeZone tz;
    if (!absl::LoadTimeZone(str, &tz)) {
        absl::LoadTimeZone("Europe/Moscow", &tz);
        return tz;
    }
    return tz;
}
