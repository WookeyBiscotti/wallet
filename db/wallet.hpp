#pragma once

#include "../utils.hpp"

#include <SQLiteCpp/SQLiteCpp.h>

#include <absl/time/time.h>

#include <fmt/format.h>

#include <cstdint>

struct Wallet {
    std::int64_t chatId;
    absl::TimeZone timeZone;
    double dayLimit;

    void save(SQLite::Database& db) const {
        db.exec(
            fmt::format("INSERT OR REPLACE INTO Wallets VALUES({}, \"{}\", {}) ", chatId, timeZone.name(), dayLimit));
    }

    template<class Fn>
    static void loadForEach(SQLite::Database& db, Fn&& fn) {
        SQLite::Statement query(db, fmt::format("SELECT * FROM Wallets"));
        while (query.executeStep()) {
            Wallet wallet;
            wallet.chatId = query.getColumn(0).getInt64();
            wallet.timeZone = getTimeZone(query.getColumn(1).getString());
            wallet.dayLimit = query.getColumn(2).getDouble();
            fn(wallet);
        }
    }
};
