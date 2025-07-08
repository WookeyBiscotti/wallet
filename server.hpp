#pragma once

#include "db/day_report.hpp"
#include "db/entry_tag.hpp"
#include "db/tag.hpp"
#include "db/wallet.hpp"
#include "db/wallet_entry.hpp"

#include "query_commands.hpp"

#include "migration.hpp"
#include "utils.hpp"

#include <cstdint>
#include <iostream>
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

            _bot->getApi().sendMessage(chat->id, "❔ Добавить тэг?", nullptr, nullptr,
                Tag::createTagsKeyboard(_db, chat->id, entry.id));

            if (wallet.dayLimit == 0) {
                return;
            }
            const auto delta = wallet.dayLimit - WalletEntry::getDayAmountSum(_db, wallet).amount;
            std::string message;
            if (delta < 0) {
                message = fmt::format("🟥 Дефицит дня: {:.0f}₽", -delta);
            } else {
                message = fmt::format("🟩 Осталось на день: {:.0f}₽", delta);
            }

            _bot->getApi().sendMessage(chat->id, message);
        });

        TgBot::BotCommand::Ptr cmdArray(new TgBot::BotCommand);
        cmdArray->command = "sumday";
        cmdArray->description = "Сумма за день";
        commands.push_back(cmdArray);
        addCommand("sumday", [&](TgBot::Message::Ptr msg) {
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
        addCommand("stat_ten", [&](TgBot::Message::Ptr msg) {
            auto chat = msg->chat;
            if (!chat) {
                return;
            }

            auto wallet = loadWallet(chat->id);

            std::string message;
            double total = 0;
            auto data = WalletEntry::getDaysAmountSum(_db, wallet, 10);
            for (std::size_t i = 0; i != 10; ++i) {
                message += fmt::format("📅 {} 💲 {:.0f}\n", data[i].day, data[i].amount);
                total += data[i].amount;
            }
            message += fmt::format("━━━━━━━━━━━━━\n💰 = {:.0f}₽", total);

            _bot->getApi().sendMessage(chat->id, message);
        });
        cmdArray = TgBot::BotCommand::Ptr(new TgBot::BotCommand);
        cmdArray->command = "/set_day_limit";
        cmdArray->description = "Установить дневной лимит";
        commands.push_back(cmdArray);
        addCommand("set_day_limit", [&](TgBot::Message::Ptr msg) {
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
        addCommand("get_day_limit", [&](TgBot::Message::Ptr msg) {
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
                auto report = DayReport::load(_db, wallet, lastDay - i);
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

        addCommand("report", [&](TgBot::Message::Ptr msg) {
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
        addCommand("report_1", [&](TgBot::Message::Ptr msg) { reportFn(msg, 1); });

        cmdArray = TgBot::BotCommand::Ptr(new TgBot::BotCommand);
        cmdArray->command = "/report_7";
        cmdArray->description = "Узнать отчет за предыдущую неделю";
        commands.push_back(cmdArray);
        addCommand("report_7", [&](TgBot::Message::Ptr msg) { reportFn(msg, 7); });

        cmdArray = TgBot::BotCommand::Ptr(new TgBot::BotCommand);
        cmdArray->command = "/add_tag";
        cmdArray->description = "Добавить тэг трат";
        commands.push_back(cmdArray);
        addCommand("add_tag", [&](TgBot::Message::Ptr msg) {
            auto chat = msg->chat;
            if (!chat) {
                return;
            }

            std::vector<std::string_view> strings = absl::StrSplit(std::string_view(msg->text), ' ');

            if (strings.size() < 2) {
                _bot->getApi().sendMessage(chat->id, "⚠️ Необходимо указать тег. Например: `/add_tag 🍟 Еда`");
                return;
            }

            const auto tag = std::string(strings[1].data(), strings.back().data() + strings.back().size());

            auto wallet = loadWallet(chat->id);

            Tag walletTag;
            walletTag.chatId = wallet.chatId;
            walletTag.tag = tag;

            walletTag.save(_db);

            _bot->getApi().sendMessage(chat->id, fmt::format("✅ Тэг добавлен: {}", tag));
        });

        _bot->getEvents().onCallbackQuery([&](const TgBot::CallbackQuery::Ptr query) {
            if (!query->message || !query->message->chat) {
                return;
            }
            auto chat = query->message->chat;

            std::vector<std::string_view> strings = absl::StrSplit(std::string_view(query->data), ' ');

            if (strings.size() < 1) {
                return;
            }

            if (strings[0] == DELETE_MESSAGE) {
                _bot->getApi().deleteMessage(chat->id, query->message->messageId);
            } else if (strings[0] == ADD_ENTRY_TAG) {
                if (strings.size() != 3) {
                    return;
                }

                auto entryId = strToT<std::int64_t>(strings[1]);
                if (!entryId) {
                    return;
                }

                auto tagId = strToT<std::int64_t>(strings[2]);
                if (!tagId) {
                    return;
                }

                EntryTag eTag;
                eTag.entryId = *entryId;
                eTag.tagId = *tagId;
                if (!eTag.save(_db)) {
                    return;
                }

                _bot->getApi().deleteMessage(chat->id, query->message->messageId);
            }
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
        Wallet::loadForEach(_db, [&](const Wallet& wallet) { _wallets.emplace(wallet.chatId, wallet); });
    }

    template<class Fn>
    void addCommand(const std::string& name, Fn&& fn) {
        _bot->getEvents().onCommand(name, [&, fn = std::move(fn)](TgBot::Message::Ptr msg) {
            try {
                fn(msg);
            } catch (const std::exception& e) {
                if (msg->chat) {
                    _bot->getApi().sendMessage(msg->chat->id,
                        fmt::format("⚠️ Ошибка при выполнении команды: {}", e.what()));
                }
            }
        });
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
