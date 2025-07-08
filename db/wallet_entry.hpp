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
    std::int64_t messageId;

    void save(SQLite::Database& db) {
        SQLite::Statement query(db, fmt::format("INSERT INTO Entries VALUES(NULL,{},{},{},\"{}\", {}) RETURNING *;",
                                        chatId, absl::ToUnixSeconds(time), amount, description, messageId));
        query.executeStep();
        id = query.getColumn(0).getInt64();
    }

    template<class Fn>
    static void loadForEach(SQLite::Database& db, std::int64_t chatId, absl::Time first, absl::Time last, Fn&& fn) {
        SQLite::Statement query(db, fmt::format("SELECT * FROM Entries WHERE chat_id = {} AND ts >= {} AND ts <= {}",
                                        chatId, absl::ToUnixSeconds(first), absl::ToUnixSeconds(last)));
        while (query.executeStep()) {
            WalletEntry entry;
            entry.id = query.getColumn(0).getInt64();
            entry.chatId = query.getColumn(1).getInt64();
            entry.time = absl::FromUnixSeconds(query.getColumn(2).getInt64());
            entry.amount = query.getColumn(3).getDouble();
            entry.description = query.getColumn(4).getString();
            entry.messageId = query.getColumn(5).getInt64();

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

    struct TagsReport {
        std::unordered_map<std::int64_t, double> byTags;
        double total;
        double withoutTags;
    };

    static TagsReport getReportByTags(SQLite::Database& db, const Wallet& wallet, std::size_t daysCount) {
        const auto toTs = absl::Now();
        const auto fromTs = absl::FromCivil(absl::ToCivilDay(toTs, wallet.timeZone) - daysCount, wallet.timeZone);

        TagsReport report{};

        SQLite::Statement query(db,
            fmt::format("SELECT amount, tag_id FROM Entries LEFT JOIN "
                        "EntryTags ON Entries.id=EntryTags.entry_id WHERE chat_id = {} AND ts >= {} AND ts <= {} ",
                wallet.chatId, absl::ToUnixSeconds(fromTs), absl::ToUnixSeconds(toTs)));
        while (query.executeStep()) {
            auto amount = query.getColumn(0).getDouble();
            report.total += amount;
            if (query.isColumnNull(1)) {
                report.withoutTags += amount;
            } else {
                report.byTags[query.getColumn(1).getInt64()] += amount;
            }
        }

        return report;
    }
};
