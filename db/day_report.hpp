#pragma once

#include "../utils.hpp"
#include "wallet.hpp"
#include "wallet_entry.hpp"

#include <SQLiteCpp/SQLiteCpp.h>

#include <absl/time/clock.h>
#include <absl/time/time.h>

#include <fmt/format.h>

#include <cstdint>

static inline std::int64_t dateToInt(absl::CivilDay day) {
    return day.day() + day.month() * 100 + day.year() * 10000;
}

static inline absl::CivilDay intToDate(std::int64_t dayInt) {
    return absl::CivilDay(dayInt / 10000, (dayInt / 100) % 100, dayInt % 100);
}

struct DayReport {
    std::int64_t chatId;
    absl::CivilDay date;
    double dayExpenses;
    double dayBalance;
    double dayLimit;

    std::string dayColor() const {
        std::string color;
        if (dayBalance < 0) {
            if (dayLimit - dayExpenses < 0) {
                color = "🟥";
            } else {
                color = "🟧";
            }
        } else {
            if (dayLimit - dayExpenses < 0) {
                color = "🟨";
            } else {
                color = "🟩";
            }
        }
        return color;
    }

    std::string toString() const {
        return fmt::format("📅{:02d}/{:02d}:💸{:>6}₽,⚖️{}{:>6}₽", date.day(), date.month(),
            formatWithApostrophes(dayExpenses), dayColor(), formatWithApostrophes(dayBalance));
    }

    static std::optional<DayReport> load(SQLite::Database& db, const Wallet& wallet, absl::CivilDay day) {
        const auto nowDay = absl::ToCivilDay(absl::Now(), wallet.timeZone);
        if (nowDay <= day) {
            return std::nullopt;
        }

        SQLite::Statement queryFirstWalletEntry(db,
            fmt::format("SELECT MIN(ts) FROM Entries WHERE chat_id = {}", wallet.chatId));

        if (!queryFirstWalletEntry.executeStep()) {
            DayReport report;
            report.chatId = wallet.chatId;
            report.date = day;
            report.dayLimit = wallet.dayLimit;
            report.dayExpenses = 0;
            report.dayBalance = report.dayLimit - report.dayExpenses;

            return report;
        }

        const auto firstWalletEntryTs = queryFirstWalletEntry.getColumn(0).getInt64();
        const auto firstDay = absl::ToCivilDay(absl::FromUnixSeconds(firstWalletEntryTs), wallet.timeZone);

        return load(db, wallet, day, firstDay);
    }

    void save(SQLite::Database& db) {
        db.exec(fmt::format("INSERT INTO DayReports VALUES({},{},{},{},{})", chatId, dateToInt(date), dayExpenses,
            dayBalance, dayLimit));
    }

private:
    static std::optional<DayReport> load(SQLite::Database& db, const Wallet& wallet, absl::CivilDay day,
        absl::CivilDay firstWalletEntryDay) {

        if (day < firstWalletEntryDay) {
            return std::nullopt;
        }

        SQLite::Statement query(db,
            fmt::format("SELECT * FROM DayReports WHERE chat_id = {} AND date = {}", wallet.chatId, dateToInt(day)));

        if (query.executeStep()) {
            DayReport report;
            report.chatId = query.getColumn(0).getInt64();
            report.date = intToDate(query.getColumn(1).getInt64());
            report.dayExpenses = query.getColumn(2).getDouble();
            report.dayBalance = query.getColumn(3).getDouble();
            report.dayLimit = query.getColumn(4).getDouble();

            return report;
        }

        auto dayBeforeReport = load(db, wallet, day - 1, firstWalletEntryDay);

        DayReport report;
        report.chatId = wallet.chatId;
        report.date = day;
        report.dayExpenses = WalletEntry::getDayAmountSum(db, wallet, day).amount;
        report.dayLimit = wallet.dayLimit;

        if (!dayBeforeReport) {
            report.dayBalance = report.dayLimit - report.dayExpenses;
        } else {
            report.dayBalance = dayBeforeReport->dayBalance + report.dayLimit - report.dayExpenses;
        }

        report.save(db);

        return report;
    }
};
