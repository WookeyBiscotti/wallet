#pragma once

#include <SQLiteCpp/SQLiteCpp.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <vector>

class Migration {
public:
    Migration(const std::filesystem::path& rootDir, SQLite::Database& db) {
        if (!db.tableExists("Migration")) {
            db.exec("CREATE TABLE Migration (version INTEGER)");
            db.exec("INSERT INTO Migration VALUES(0)");
        }

        auto migrationDir = rootDir / "migration";
        std::error_code ec;
        std::filesystem::create_directories(migrationDir, ec);

        std::vector<std::filesystem::path> paths;
        for (auto const& entry : std::filesystem::directory_iterator{migrationDir}) {
            paths.push_back(entry);
        }

        std::sort(paths.begin(), paths.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
            return std::stoi(a.filename().replace_extension("")) < std::stoi(b.filename().replace_extension(""));
        });

        SQLite::Statement query(db, "SELECT version FROM Migration");
        query.executeStep();
        auto currentVersion = query.getColumn(0).getInt();

        SQLite::Transaction tr(db);
        for (auto const& entry : paths) {
            auto migrationVersion = std::stoi(entry.filename().replace_extension(""));
            if (migrationVersion > currentVersion) {
                currentVersion = migrationVersion;
                std::ifstream а(entry);
                std::string queryStr((std::istreambuf_iterator<char>(а)), std::istreambuf_iterator<char>());
                db.exec(queryStr);
            }
        }
        db.exec(std::format("UPDATE Migration SET version = {}", currentVersion));
        tr.commit();
    }
};
