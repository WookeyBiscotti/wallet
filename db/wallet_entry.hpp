#pragma once

#include "wallet.hpp"

#include <SQLiteCpp/SQLiteCpp.h>

#include <absl/container/inlined_vector.h>
#include <absl/time/clock.h>
#include <absl/time/time.h>

#include <fmt/format.h>

#include <cstdint>

struct WalletEntry {
    std::int64_t id;
    std::int64_t chatId;
    absl::Time time;
    double amount;
    std::string description;

    void save(SQLite::Database& db) {
        db.exec(fmt::format("INSERT INTO WalletEntries VALUES(NULL,{},{},{},\"{}\")", chatId, absl::ToUnixSeconds(time),
            amount, description));
    }

    template<class Fn>
    static void loadForEach(SQLite::Database& db, std::int64_t chatId, absl::Time first, absl::Time last, Fn&& fn) {
        SQLite::Statement query(db,
            fmt::format("SELECT * FROM WalletEntries WHERE chat_id = {} AND ts >= {} AND ts <= {}", chatId,
                absl::ToUnixSeconds(first), absl::ToUnixSeconds(last)));
        while (query.executeStep()) {
            WalletEntry entry;
            entry.id = query.getColumn(0).getInt64();
            entry.chatId = query.getColumn(1).getInt64();
            entry.time = absl::FromUnixSeconds(query.getColumn(2).getInt64());
            entry.amount = query.getColumn(3).getDouble();
            entry.description = query.getColumn(4).getDouble();

            fn(entry);
        }
    }

    struct DaySumInfo {
        double amount;
        std::string day;
    };

    static DaySumInfo getDayAmountSum(SQLite::Database& db, const Wallet& wallet) {
        return getDaysAmountSum(db, wallet, 1)[0];
    }

    static DaySumInfo getDayAmountSum(SQLite::Database& db, const Wallet& wallet, absl::CivilDay day) {
        return getDaysAmountSum(db, wallet, day, 1)[0];
    }

    static absl::InlinedVector<DaySumInfo, 10> getDaysAmountSum(SQLite::Database& db, const Wallet& wallet,
        absl::CivilDay day, std::size_t daysCount) {
        absl::InlinedVector<DaySumInfo, 10> result;

        const auto& tz = wallet.timeZone;

        for (std::size_t i = 0; i != daysCount; ++i) {
            auto dayStart = absl::FromCivil(day - i, tz);
            auto dayEnd = absl::FromCivil(day - i + 1, tz);

            double sum = 0;
            WalletEntry::loadForEach(db, wallet.chatId, dayStart, dayEnd,
                [&](const WalletEntry& entry) { sum += entry.amount; });

            result.push_back({sum, absl::FormatTime("%d/%m/%Y", dayStart, tz)});
        }

        return result;
    }

    static absl::InlinedVector<DaySumInfo, 10> getDaysAmountSum(SQLite::Database& db, const Wallet& wallet,
        std::size_t daysCount) {

        const auto nowDay = absl::ToCivilDay(absl::Now(), wallet.timeZone);

        return getDaysAmountSum(db, wallet, nowDay, daysCount);
    }
};
