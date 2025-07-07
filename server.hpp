#pragma once

#include "db/wallet.hpp"
#include "db/wallet_day_report.hpp"
#include "db/wallet_entry.hpp"

#include "migration.hpp"
#include "scheduler.hpp"
#include "utils.hpp"

#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <absl/container/inlined_vector.h>
#include <absl/strings/charconv.h>
#include <absl/strings/str_split.h>
#include <absl/time/clock.h>
#include <absl/time/time.h>

#include <SQLiteCpp/SQLiteCpp.h>

#include <tgbot/Bot.h>
#include <tgbot/net/CurlHttpClient.h>
#include <tgbot/net/TgLongPoll.h>
#include <tgbot/types/ReactionTypeEmoji.h>

#include <fmt/format.h>

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

        _bot.emplace(*token, _curlHttpClient);
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

            auto amount = strToDouble(strings[0]);
            if (!amount) {
                return;
            }

            auto wallet = loadWallet(chat->id);

            WalletEntry entry;
            entry.amount = *amount;
            entry.description = std::string(strings[1].data(), strings.back().data() + strings.back().size());
            entry.time = absl::FromUnixSeconds(msg->date);
            entry.chatId = chat->id;

            entry.save(_db);

            _bot->getApi().setMessageReaction(chat->id, msg->messageId, {[] {
                auto r = std::make_shared<TgBot::ReactionTypeEmoji>();
                r->emoji = "⚡";
                return std::move(r);
            }()},
                true);

            tr.commit();

            if (wallet.dayLimit == 0) {
                return;
            }
            const auto delta = wallet.dayLimit - WalletEntry::getDayAmountSum(_db, wallet).amount;
            std::string message;
            if (delta < 0) {
                message = fmt::format("🟥 Дефицит дня: {:'.0f}₽", -delta);
            } else {
                message = fmt::format("🟩 Осталось на день: {:'.0f}₽", delta);
            }

            _bot->getApi().sendMessage(chat->id, message);
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

            auto wallet = loadWallet(chat->id);

            _bot->getApi().sendMessage(chat->id,
                fmt::format("{:.0f}", WalletEntry::getDayAmountSum(_db, wallet).amount));
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

            auto wallet = loadWallet(chat->id);

            std::string message;
            double total = 0;
            auto data = WalletEntry::getDaysAmountSum(_db, wallet, 10);
            for (std::size_t i = 0; i != 10; ++i) {
                message += fmt::format("📅 {} 💲 {:'.0f}\n", data[i].day, data[i].amount);
                total += data[i].amount;
            }
            message += fmt::format("━━━━━━━━━━━━━\n💰 = {:'.0f}₽", total);

            _bot->getApi().sendMessage(chat->id, message);
        });
        cmdArray = TgBot::BotCommand::Ptr(new TgBot::BotCommand);
        cmdArray->command = "/set_day_limit";
        cmdArray->description = "Установить дневной лимит";
        commands.push_back(cmdArray);
        _bot->getEvents().onCommand("set_day_limit", [&](TgBot::Message::Ptr msg) {
            auto chat = msg->chat;
            if (!chat) {
                return;
            }

            std::vector<std::string_view> strings = absl::StrSplit(std::string_view(msg->text), ' ');

            if (strings.size() != 2) {
                _bot->getApi().sendMessage(chat->id,
                    "⚠️ Необходимо указать дневной лимит. Например: `/set_day_limit 1337`");
                return;
            }

            auto dayLimit = strToDouble(strings[1]);
            if (!dayLimit) {
                _bot->getApi().sendMessage(chat->id, "⚠️ Дневной лимит должен быть числом. Например: `1337`");

                return;
            }

            SQLite::Transaction tr(_db);

            auto wallet = loadWallet(chat->id);
            wallet.dayLimit = *dayLimit;
            updateWallet(chat->id, wallet);

            _bot->getApi().setMessageReaction(chat->id, msg->messageId, {[] {
                auto r = std::make_shared<TgBot::ReactionTypeEmoji>();
                r->emoji = "⚡";
                return std::move(r);
            }()},
                true);

            tr.commit();
        });
        cmdArray = TgBot::BotCommand::Ptr(new TgBot::BotCommand);
        cmdArray->command = "/get_day_limit";
        cmdArray->description = "Узнать дневной лимит";
        commands.push_back(cmdArray);
        _bot->getEvents().onCommand("get_day_limit", [&](TgBot::Message::Ptr msg) {
            auto chat = msg->chat;
            if (!chat) {
                return;
            }

            auto wallet = loadWallet(chat->id);
            _bot->getApi().sendMessage(chat->id, fmt::format("🕑💰 Дневной лимит: {}", wallet.dayLimit));
        });

        cmdArray = TgBot::BotCommand::Ptr(new TgBot::BotCommand);
        cmdArray->command = "/report";
        cmdArray->description = "Узнать отчет за N дней";
        commands.push_back(cmdArray);

        auto reportFn = [&](TgBot::Message::Ptr msg, std::size_t daysCount) {
            auto chat = msg->chat;
            if (!chat) {
                return;
            }

            auto wallet = loadWallet(chat->id);
            const auto lastDay = absl::ToCivilDay(absl::Now(), wallet.timeZone) - 1;

            std::string reportStr;
            for (std::size_t i = 0; i != daysCount; ++i) {
                auto report = WalletDayReport::load(_db, wallet, lastDay - i);
                if (report) {
                    reportStr += "`" + report->toString() + "`\n";
                } else {
                    break;
                }
            }

            if (!reportStr.empty()) {
                _bot->getApi().sendMessage(chat->id, reportStr, nullptr, nullptr, nullptr, "MarkdownV2");
            } else {
                _bot->getApi().sendMessage(chat->id,
                    fmt::format("⚠️ Невозможно получить отчет за этот день: 📅 {:02d}/{:02d}/{}", lastDay.day(),
                        lastDay.month(), lastDay.year()));
            }
        };

        _bot->getEvents().onCommand("report", [&](TgBot::Message::Ptr msg) {
            auto chat = msg->chat;
            if (!chat) {
                return;
            }

            std::vector<std::string_view> strings = absl::StrSplit(std::string_view(msg->text), ' ');

            if (strings.size() != 2) {
                _bot->getApi().sendMessage(chat->id, "⚠️ Необходимо указать количество дней. Например: `/report 7`");
                return;
            }

            auto daysCount = strToT<std::size_t>(strings[1]);
            if (!daysCount) {
                _bot->getApi().sendMessage(chat->id, "⚠️ Количество дней должно быть числом. Например: `7`");

                return;
            }

            reportFn(msg, *daysCount);
        });

        cmdArray = TgBot::BotCommand::Ptr(new TgBot::BotCommand);
        cmdArray->command = "/report_1";
        cmdArray->description = "Узнать отчет за предыдущий день";
        commands.push_back(cmdArray);
        _bot->getEvents().onCommand("report_1", [&](TgBot::Message::Ptr msg) { reportFn(msg, 1); });

        cmdArray = TgBot::BotCommand::Ptr(new TgBot::BotCommand);
        cmdArray->command = "/report_7";
        cmdArray->description = "Узнать отчет за предыдущую неделю";
        commands.push_back(cmdArray);
        _bot->getEvents().onCommand("report_7", [&](TgBot::Message::Ptr msg) { reportFn(msg, 7); });

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
        Wallet::loadForEach(_db, [&](const Wallet& wallet) { _wallets.emplace(wallet.chatId, wallet); });
    }

    Wallet loadWallet(std::int64_t chatId) {
        auto foundChat = _wallets.find(chatId);
        if (foundChat == _wallets.end()) {
            Wallet w = {};
            w.save(_db);
            foundChat = _wallets.emplace(chatId, std::move(w)).first;
        }
        return foundChat->second;
    }

    void updateWallet(std::int64_t chatId, const Wallet& wallet) {
        auto foundChat = _wallets.find(chatId);
        if (foundChat == _wallets.end()) {
            wallet.save(_db);

            foundChat = _wallets.emplace(chatId, wallet).first;
        } else {
            foundChat->second = wallet;
            wallet.save(_db);
        }
    }

private:
    SQLite::Database _db;
    std::optional<TgBot::Bot> _bot;

    std::unordered_map<std::int64_t, Wallet> _wallets;
    TgBot::CurlHttpClient _curlHttpClient;
};
