#pragma once

#include "migration.hpp"
#include "utils.hpp"

#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <absl/container/inlined_vector.h>
#include <absl/strings/charconv.h>
#include <absl/strings/str_split.h>
#include <absl/time/clock.h>
#include <absl/time/time.h>

#include <SQLiteCpp/SQLiteCpp.h>

#include <tgbot/Bot.h>
#include <tgbot/net/TgLongPoll.h>
#include <tgbot/types/ReactionTypeEmoji.h>

#include <fmt/format.h>

struct WalletEntry {
    std::int64_t id;
    double amount;
    std::string description;

    void save(SQLite::Database& db, std::int64_t chatId, const absl::Time& time) {
        db.exec(fmt::format("INSERT INTO WalletEntries VALUES(NULL,{},{},{},\"{}\")", chatId, absl::ToUnixSeconds(time),
            amount, description));
    }
};

inline std::multimap<absl::Time, WalletEntry> loadWalletEntries(SQLite::Database& db, std::int64_t chatId) {
    std::multimap<absl::Time, WalletEntry> entries;
    SQLite::Statement query(db, fmt::format("SELECT * FROM WalletEntries WHERE chat_id = {}", chatId));
    while (query.executeStep()) {
        WalletEntry entry;
        entry.id = query.getColumn(0).getInt64();
        entry.amount = query.getColumn(3).getDouble();
        entry.description = query.getColumn(4).getDouble();
        const auto ts = query.getColumn(2).getInt64();

        entries.emplace(absl::FromUnixSeconds(ts), entry);
    }

    return entries;
}

struct Wallet {
    std::string timeZone = "Europe/Moscow";
    double dayLimit;

    bool load(SQLite::Database& db, std::int64_t chatId) {
        {
            SQLite::Statement query(db, fmt::format("SELECT * FROM Wallets WHERE chat_id = {}", chatId));
            if (!query.executeStep()) {
                return false;
            }
            timeZone = query.getColumn(1).getString();
            dayLimit = query.getColumn(2).getDouble();
        }

        return true;
    }

    void save(SQLite::Database& db, std::int64_t chatId) {
        auto qStr = fmt::format("INSERT INTO Wallets VALUES({}, \"{}\")", chatId, timeZone);
        db.exec(qStr);
    }
};

class Server {
public:
    Server(const std::filesystem::path& rootDir):
        _db(rootDir / "wallet.db", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE) {
        Migration(rootDir, _db);

        auto token = findToken(rootDir);
        if (!token) {
            exit(0);
        }

        loadWallets();

        _bot.emplace(*token);
        _bot->getApi().deleteWebhook();

        std::vector<TgBot::BotCommand::Ptr> commands;
        _bot->getEvents().onAnyMessage([&](TgBot::Message::Ptr msg) {
            SQLite::Transaction tr(_db);
            auto chat = msg->chat;
            if (!chat) {
                return;
            }
            std::vector<std::string_view> strings = absl::StrSplit(std::string_view(msg->text), ' ');
            if (strings.size() < 2) {
                return;
            }

            double amount;
            if (absl::from_chars(strings[0].begin(), strings[0].end(), amount).ec != std::errc()) {
                return;
            }

            auto foundChat = _wallets.find(chat->id);
            if (foundChat == _wallets.end()) {
                Wallet w;
                w.save(_db, chat->id);
                foundChat = _wallets.emplace(chat->id, std::move(w)).first;
            }

            WalletEntry entry;
            entry.amount = amount;
            entry.description = std::string(strings[1].data(), strings.back().data() + strings.back().size());

            absl::Time time = absl::FromUnixSeconds(msg->date);

            entry.save(_db, chat->id, time);

            _bot->getApi().setMessageReaction(chat->id, msg->messageId, {[] {
                auto r = std::make_shared<TgBot::ReactionTypeEmoji>();
                r->emoji = "⚡";
                return std::move(r);
            }()},
                true);

            tr.commit();
        });

        TgBot::BotCommand::Ptr cmdArray(new TgBot::BotCommand);
        cmdArray->command = "sumday";
        cmdArray->description = "Сумма за день";
        commands.push_back(cmdArray);
        _bot->getEvents().onCommand("sumday", [&](TgBot::Message::Ptr msg) {
            auto chat = msg->chat;
            if (!chat) {
                return;
            }

            _bot->getApi().sendMessage(chat->id, fmt::format("{:.0f}", getDayAmountSum(chat->id).amount));
        });

        cmdArray = TgBot::BotCommand::Ptr(new TgBot::BotCommand);
        cmdArray->command = "stat_ten";
        cmdArray->description = "Статистика за 10 дней";
        commands.push_back(cmdArray);
        _bot->getEvents().onCommand("stat_ten", [&](TgBot::Message::Ptr msg) {
            auto chat = msg->chat;
            if (!chat) {
                return;
            }
            std::string message;
            double total = 0;
            auto data = getDaysAmountSum(chat->id, 10);
            for (std::size_t i = 0; i != 10; ++i) {
                message += fmt::format("📅{} 💲{:.0f}\n", data[i].day, data[i].amount);
                total += data[i].amount;
            }
            message += fmt::format("━━━━━━━━━━━━━\n💰 = {}", total);

            _bot->getApi().sendMessage(chat->id, message);
        });

        _bot->getApi().setMyCommands(commands);
        TgBot::TgLongPoll longPoll(*_bot);
        while (true) {
            try {
                longPoll.start();
            } catch (const std::exception& e) {
                std::cout << e.what();
            }
        }
    }

private:
    void loadWallets() {
        SQLite::Statement query(_db, fmt::format("SELECT * FROM Wallets"));
        while (query.executeStep()) {
            Wallet wallet;
            wallet.timeZone = query.getColumn(1).getString();
            _wallets.emplace(query.getColumn(0).getInt64(), std::move(wallet));
        }
    }

    struct DaySumInfo {
        double amount;
        std::string day;
    };

    DaySumInfo getDayAmountSum(std::int64_t chatId) {
        return getDaysAmountSum(chatId, 1)[0];
    }

    absl::InlinedVector<DaySumInfo, 10> getDaysAmountSum(std::int64_t chatId, std::size_t daysCount) {
        absl::InlinedVector<DaySumInfo, 10> result;
        result.reserve(daysCount);

        auto foundChat = _wallets.find(chatId);
        if (foundChat == _wallets.end()) {
            result.resize(daysCount);

            return result;
        }

        absl::TimeZone tz;
        absl::LoadTimeZone(foundChat->second.timeZone, &tz);

        const auto now = absl::Now();
        const auto nowDate = absl::ToCivilDay(now, tz);

        for (std::size_t i = 0; i != daysCount; ++i) {
            auto dayStart = absl::FromCivil(nowDate - i, tz);
            auto dayEnd = absl::FromCivil(nowDate - i + 1, tz);

            SQLite::Statement query(_db,
                fmt::format("SELECT amount FROM WalletEntries WHERE chat_id = {} AND ts >= {} AND ts <= {}", chatId,
                    absl::ToUnixSeconds(dayStart), absl::ToUnixSeconds(dayEnd)));
            double sum = 0;
            while (query.executeStep()) {
                sum += query.getColumn(0).getDouble();
            }
            result.push_back({sum, absl::FormatTime("%d/%m/%Y", dayStart, tz)});
        }

        return result;
    }

private:
    SQLite::Database _db;
    std::optional<TgBot::Bot> _bot;

    std::unordered_map<std::int64_t, Wallet> _wallets;
};
