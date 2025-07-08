#pragma once

#include "../query_commands.hpp"
#include "../utils.hpp"
#include "wallet.hpp"
#include "wallet_entry.hpp"

#include <SQLiteCpp/SQLiteCpp.h>

#include <tgbot/tgbot.h>

#include <absl/time/clock.h>
#include <absl/time/time.h>

#include <fmt/format.h>

#include <cstdint>

struct Tag {
    std::int64_t id;
    std::int64_t chatId;
    std::string tag;

    void save(SQLite::Database& db) const {
        SQLite::Statement saver(db, fmt::format("INSERT OR REPLACE INTO Tags VALUES(NULL, {}, ?)", chatId));
        saver.bind(1, tag);
        saver.exec();
    }

    template<class Fn>
    static void loadForEach(SQLite::Database& db, std::int64_t chatId, Fn&& fn) {
        SQLite::Statement query(db, fmt::format("SELECT * FROM Tags WHERE chat_id = {}", chatId));
        while (query.executeStep()) {
            Tag tag;
            tag.id = query.getColumn(0).getInt64();
            tag.chatId = query.getColumn(1).getInt64();
            tag.tag = query.getColumn(2).getString();

            fn(std::move(tag));
        }
    }

    static std::unordered_map<std::uint64_t, std::string> tagsIdToStr(SQLite::Database& db, std::int64_t chatId) {
        std::unordered_map<std::uint64_t, std::string> tags;

        loadForEach(db, chatId, [&](Tag tag) { tags[tag.id] = tag.tag; });

        return tags;
    }

    static TgBot::InlineKeyboardMarkup::Ptr createTagsKeyboard(SQLite::Database& db, std::int64_t chatId,
        std::int64_t entryId, std::int64_t messageId) {
        std::vector<Tag> tags;

        loadForEach(db, chatId, [&](Tag tag) { tags.push_back(tag); });
        if (tags.empty()) {
            return nullptr;
        }

        TgBot::InlineKeyboardMarkup::Ptr keyboard(new TgBot::InlineKeyboardMarkup);

        TgBot::InlineKeyboardButton::Ptr cancelButton(new TgBot::InlineKeyboardButton);
        cancelButton->text = "⬜ Добавить без тэга";
        cancelButton->callbackData = fmt::format("{}", DELETE_MESSAGE);

        TgBot::InlineKeyboardButton::Ptr refreshButton(new TgBot::InlineKeyboardButton);
        refreshButton->text = "🔄 Обновить тэги";
        refreshButton->callbackData = fmt::format("{} {} {}", REFRESH_TAGS, entryId, absl::ToUnixSeconds(absl::Now()));
        keyboard->inlineKeyboard.push_back({cancelButton, refreshButton});

        std::vector<TgBot::InlineKeyboardButton::Ptr> currentRow;
        std::size_t i = 0;
        for (const auto& t : tags) {
            if (i++ == 3) {
                keyboard->inlineKeyboard.push_back(currentRow);
                currentRow.clear();
                i = 0;
            }
            TgBot::InlineKeyboardButton::Ptr button(new TgBot::InlineKeyboardButton);
            button->text = t.tag;
            button->callbackData = fmt::format("{} {} {}", ADD_ENTRY_TAG, entryId, t.id);
            currentRow.push_back(button);
        }

        if (!currentRow.empty()) {
            keyboard->inlineKeyboard.push_back(currentRow);
            currentRow.clear();
        }

        return keyboard;
    }
};
