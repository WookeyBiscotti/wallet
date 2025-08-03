#pragma once

#include "db/day_report.hpp"
#include "db/entry_tag.hpp"
#include "db/tag.hpp"
#include "db/wallet.hpp"
#include "db/wallet_entry.hpp"
#include "renderer.hpp"
#include "table.hpp"

#include "query_commands.hpp"

#include "migration.hpp"
#include "utils.hpp"

#include <absl/container/inlined_vector.h>
#include <absl/strings/charconv.h>
#include <absl/strings/str_replace.h>
#include <absl/strings/str_split.h>
#include <absl/time/clock.h>
#include <absl/time/time.h>

#include <SQLiteCpp/SQLiteCpp.h>

#include <fort.hpp>

#include <tgbot/Bot.h>
#include <tgbot/net/CurlHttpClient.h>
#include <tgbot/net/TgLongPoll.h>
#include <tgbot/types/ReactionTypeEmoji.h>

#include <fmt/format.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

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
            try {
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
                entry.messageId = msg->messageId;

                entry.save(_db);

                _bot->getApi().setMessageReaction(chat->id, msg->messageId, {[] {
                    auto r = std::make_shared<TgBot::ReactionTypeEmoji>();
                    r->emoji = "⚡";
                    return std::move(r);
                }()},
                    true);

                tr.commit();

                if (auto tagsKeyboard = Tag::createTagsKeyboard(_db, chat->id, entry.id, msg->messageId)) {
                    _bot->getApi().sendMessage(chat->id, "❔ Добавить тэг?", nullptr, nullptr, tagsKeyboard);
                }

                if (wallet.dayLimit == 0) {
                    return;
                }
                const auto delta = wallet.dayLimit - WalletEntry::getDayAmountSum(_db, wallet).amount;
                std::string message;
                if (delta < 0) {
                    message = fmt::format("🟥 Дефицит дня: {:.0f}", -delta);
                } else {
                    message = fmt::format("🟩 Осталось на день: {:.0f}", delta);
                }

                _bot->getApi().sendMessage(chat->id, message);
            } catch (const std::exception& e) {
                if (msg->chat) {
                    _bot->getApi().sendMessage(msg->chat->id,
                        fmt::format("⚠️ Ошибка при выполнении команды: {}", e.what()));
                }
            }
        });
        addCommand("sumday", "Сумма за день", [&](TgBot::Message::Ptr msg) {
            auto chat = msg->chat;
            if (!chat) {
                return;
            }

            auto wallet = loadWallet(chat->id);

            _bot->getApi().sendMessage(chat->id,
                fmt::format("{:.0f}", WalletEntry::getDayAmountSum(_db, wallet).amount));
        });
        addCommand("stat_ten", "Статистика за 10 дней", [&](TgBot::Message::Ptr msg) {
            auto chat = msg->chat;
            if (!chat) {
                return;
            }

            auto wallet = loadWallet(chat->id);

            Table table2;
            table2.setSize({2, 1});
            table2.setContentLastRow(0, "Дата 📅");
            table2.setContentLastRow(1, "Траты 💸");
            table2.pushRow();

            double total = 0;
            auto data = WalletEntry::getDaysAmountSum(_db, wallet, 10);
            for (std::size_t i = 0; i != 10; ++i) {
                table2.pushRow();
                table2.setContentLastRow(0, data[i].day);
                table2.setContentLastRow(1, data[i].amount);
                total += data[i].amount;
            }
            table2.pushRow();
            table2.pushRow();
            table2.setContentLastRow(0, "💰💲 Всего");
            table2.setContentLastRow(1, formatWithApostrophes(total));

            table2.setColumnAlign(1, Align::RIGHT);

            const auto filename = "/tmp/" + std::to_string(chat->id) + ".png";
            table2.render(filename);
            _bot->getApi().sendPhoto(chat->id, TgBot::InputFile::fromFile(filename, "image/png"));
        });

        addCommand("set_day_limit", "Установить дневной лимит", [&](TgBot::Message::Ptr msg) {
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
        addCommand("get_day_limit", "Узнать дневной лимит", [&](TgBot::Message::Ptr msg) {
            auto chat = msg->chat;
            if (!chat) {
                return;
            }

            auto wallet = loadWallet(chat->id);
            _bot->getApi().sendMessage(chat->id, fmt::format("🕑💰 Дневной лимит: {}", wallet.dayLimit));
        });

        auto reportFn = [&](TgBot::Message::Ptr msg, std::size_t daysCount) {
            auto chat = msg->chat;
            if (!chat) {
                return;
            }

            auto wallet = loadWallet(chat->id);
            const auto lastDay = absl::ToCivilDay(absl::Now(), wallet.timeZone) - 1;

            Table table2;
            table2.setSize({4, 1});
            table2.setContentLastRow(0, "Дата 📅");
            table2.setContentLastRow(1, "Траты 💸");
            table2.setContentLastRow(2, "Баланс ⚖️");
            table2.pushRow();

            double totalSum = {};
            for (std::size_t i = 0; i != daysCount; ++i) {
                auto report = DayReport::load(_db, wallet, lastDay - i);
                if (report) {
                    table2.pushRow();
                    table2.setContentLastRow(0, fmt::format("{:02d}/{:02d}/{}", report->date.day(),
                                                    report->date.month(), report->date.year() % 100));
                    table2.setContentLastRow(1, formatWithApostrophes(report->dayExpenses));
                    table2.setContentLastRow(2, formatWithApostrophes(report->dayBalance));
                    table2.setContentLastRow(3, report->dayColor());

                    totalSum += report->dayExpenses;
                } else {
                    break;
                }
            }
            table2.pushRow();
            table2.pushRow();
            table2.setContentLastRow(0, "💰💲 Всего");
            table2.setContentLastRow(2, formatWithApostrophes(totalSum));

            table2.setColumnAlign(1, Align::RIGHT);
            table2.setColumnAlign(2, Align::RIGHT);

            const auto filename = "/tmp/" + std::to_string(chat->id) + ".png";
            table2.render(filename);
            _bot->getApi().sendPhoto(chat->id, TgBot::InputFile::fromFile(filename, "image/png"));
        };

        addCommand("report", "Узнать отчет за N дней", [&](TgBot::Message::Ptr msg) {
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
        addCommand("report_1", "Узнать отчет за предыдущий день", [&](TgBot::Message::Ptr msg) { reportFn(msg, 1); });
        addCommand("report_7", "Узнать отчет за предыдущую неделю", [&](TgBot::Message::Ptr msg) { reportFn(msg, 7); });
        addCommand("report_30", "Узнать отчет за предыдущий месяц",
            [&](TgBot::Message::Ptr msg) { reportFn(msg, 30); });
        addCommand("add_tag", "Добавить тэг трат", [&](TgBot::Message::Ptr msg) {
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
            try {
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
                } else if (strings[0] == REFRESH_TAGS) {
                    // 3rd param is dummy
                    if (strings.size() != 3) {
                        return;
                    }

                    auto entryId = strToT<std::int64_t>(strings[1]);
                    if (!entryId) {
                        return;
                    }

                    if (auto tagsKeyboard =
                            Tag::createTagsKeyboard(_db, chat->id, *entryId, query->message->messageId)) {
                        _bot->getApi().editMessageText("❔ Добавить тэг?", chat->id, query->message->messageId, "", "",
                            nullptr, tagsKeyboard);
                    }
                }
            } catch (const std::exception& e) {
                if (query->message && query->message->chat) {
                    _bot->getApi().sendMessage(query->message->chat->id,
                        fmt::format("⚠️ Ошибка при выполнении команды: {}", e.what()));
                }
            }
        });

        auto tagsReportFn = [&](TgBot::Message::Ptr msg, std::size_t daysCount) {
            auto chat = msg->chat;
            if (!chat) {
                return;
            }

            auto wallet = loadWallet(chat->id);

            auto report = WalletEntry::getReportByTags(_db, wallet, daysCount);
            auto tagsMap = Tag::tagsIdToStr(_db, chat->id);

            Table table2;
            table2.setSize({3, 1});
            table2.setContentLastRow(0, "Тэг 🏷️");
            table2.setContentLastRow(1, "Сумма 💰");
            table2.setContentLastRow(2, "Доля %");
            table2.pushRow();

            for (const auto& t : report.byTags) {
                auto tagStrIt = tagsMap.find(t.first);
                std::string_view name;
                if (tagStrIt != tagsMap.end()) {
                    name = tagStrIt->second;
                } else {
                    name = "📛 Неизвестный тэг";
                }

                table2.pushRow();
                table2.setContentLastRow(0, name);
                table2.setContentLastRow(1, fmt::format("{}", formatWithApostrophes(t.second)));
                table2.setContentLastRow(2, fmt::format("{:.0f}", 100 * t.second / report.total));
            }

            table2.pushRow();
            table2.pushRow();
            table2.setContentLastRow(0, "💰💲 Всего");
            table2.setContentLastRow(1, fmt::format("{}", formatWithApostrophes(report.total)));

            table2.setColumnAlign(1, Align::RIGHT);
            table2.setColumnAlign(2, Align::RIGHT);

            const auto filename = "/tmp/" + std::to_string(chat->id) + ".png";
            table2.render(filename);
            _bot->getApi().sendPhoto(chat->id, TgBot::InputFile::fromFile(filename, "image/png"));
        };
        addCommand("total_report", "Узнать сумарный отчет", [&](TgBot::Message::Ptr msg) {
            auto chat = msg->chat;
            if (!chat) {
                return;
            }

            std::vector<std::string_view> strings = absl::StrSplit(std::string_view(msg->text), ' ');

            if (strings.size() != 2) {
                _bot->getApi().sendMessage(chat->id,
                    "⚠️ Необходимо указать количество дней. Например: `/total_report 7`");
                return;
            }

            auto daysCount = strToT<std::size_t>(strings[1]);
            if (!daysCount) {
                _bot->getApi().sendMessage(chat->id, "⚠️ Количество дней должно быть числом. Например: `7`");

                return;
            }

            tagsReportFn(msg, *daysCount);
        });
        addCommand("total_report_1", "Узнать сумарный отчет за предыдущий день",
            [&](TgBot::Message::Ptr msg) { tagsReportFn(msg, 1); });
        addCommand("total_report_7", "Узнать сумарный отчет за предыдущую неделю",
            [&](TgBot::Message::Ptr msg) { tagsReportFn(msg, 7); });
        addCommand("total_report_30", "Узнать сумарный отчет за 30 дней",
            [&](TgBot::Message::Ptr msg) { tagsReportFn(msg, 30); });

        run();
    }

private:
    void run() {
        _bot->getApi().setMyCommands(_commands);
        TgBot::TgLongPoll longPoll(*_bot);
        while (true) {
            try {
                longPoll.start();
            } catch (const std::exception& e) {
                std::cout << e.what();
            }
        }
    }

    void loadWallets() {
        Wallet::loadForEach(_db, [&](const Wallet& wallet) { _wallets.emplace(wallet.chatId, wallet); });
    }

    template<class Fn>
    void addCommand(const std::string& name, const std::string& descr, Fn&& fn) {
        auto command = TgBot::BotCommand::Ptr(new TgBot::BotCommand);
        command->command = "/" + name;
        command->description = descr;
        _commands.push_back(std::move(command));

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

    std::vector<TgBot::BotCommand::Ptr> _commands;
};
