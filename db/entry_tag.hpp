#pragma once

#include "../utils.hpp"
#include "wallet.hpp"
#include "wallet_entry.hpp"

#include <SQLiteCpp/SQLiteCpp.h>

#include <absl/time/clock.h>
#include <absl/time/time.h>

#include <fmt/format.h>

#include <cstdint>

struct EntryTag {
    std::int64_t entryId;
    std::int64_t tagId;

    bool save(SQLite::Database& db) const {
        SQLite::Statement checkQery(db, fmt::format(R"(
    SELECT * FROM Entries
    INNER JOIN Tags ON Entries.chat_id=Tags.chat_id
    WHERE Entries.id={} AND Tags.id={};)",
                                            entryId, tagId));

        if (checkQery.executeStep()) {
            db.exec(fmt::format("INSERT OR REPLACE INTO EntryTags VALUES({}, {})", entryId, tagId));
            return true;
        }
        return false;
    }

    template<class Fn>
    static void loadForEach(SQLite::Database& db, std::int64_t entryId, Fn&& fn) {
        SQLite::Statement query(db, fmt::format("SELECT * FROM EntryTags WHERE entry_id = {}", entryId));
        EntryTag tag;
        while (query.executeStep()) {
            tag.entryId = query.getColumn(0).getInt64();
            tag.tagId = query.getColumn(1).getInt64();

            fn(std::move(tag));
        }
    }
};
