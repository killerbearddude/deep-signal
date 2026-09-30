#pragma once

// Test-only logical snapshot comparison. It enumerates every current SQLite
// table/column independently of SaveGameRepository's INSERT order, retaining
// ordinal values while ignoring physical row insertion order. Continuation
// tests also inspect live state and advance both branches after a real Load.
#include "save/SaveGameRepository.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace deep::p6test {
namespace detail {
inline std::string quotedIdentifier(const std::string& name) {
    std::string quoted = "\"";
    for (char value : name) {
        if (value == '"')
            quoted.push_back('"');
        quoted.push_back(value);
    }
    return quoted + '"';
}

inline std::string bytes(const void* pointer, int count) {
    constexpr char digits[] = "0123456789abcdef";
    std::string value;
    value.reserve(static_cast<std::size_t>(count) * 2);
    const auto* data = static_cast<const unsigned char*>(pointer);
    for (int index = 0; index < count; ++index) {
        value.push_back(digits[data[index] >> 4]);
        value.push_back(digits[data[index] & 15]);
    }
    return value;
}

inline std::string cell(sqlite3_stmt* statement, int column) {
    const int type = sqlite3_column_type(statement, column);
    if (type == SQLITE_NULL)
        return "N";
    if (type == SQLITE_INTEGER)
        return "I" + std::to_string(sqlite3_column_int64(statement, column));
    if (type == SQLITE_FLOAT) {
        const double value = sqlite3_column_double(statement, column);
        return "F" + bytes(&value, static_cast<int>(sizeof(value)));
    }
    const void* pointer = type == SQLITE_TEXT
                              ? static_cast<const void*>(sqlite3_column_text(statement, column))
                              : sqlite3_column_blob(statement, column);
    const int count = sqlite3_column_bytes(statement, column);
    return (type == SQLITE_TEXT ? "T" : "B") + std::to_string(count) + ':' + bytes(pointer, count);
}

inline std::vector<std::string> query(sqlite3* database, const std::string& sql, bool tableNames = false) {
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(database, sql.c_str(), -1, &raw, nullptr) != SQLITE_OK)
        throw std::runtime_error("P6 independent snapshot query failed: " + sql);
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw, sqlite3_finalize);
    std::vector<std::string> rows;
    int result;
    while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
        if (tableNames) {
            rows.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 0)));
            continue;
        }
        std::string row;
        for (int column = 0; column < sqlite3_column_count(statement.get()); ++column) {
            const std::string name = sqlite3_column_name(statement.get(), column);
            const std::string value = cell(statement.get(), column);
            row += std::to_string(name.size()) + ':' + name + std::to_string(value.size()) + ':' + value;
        }
        rows.push_back(std::move(row));
    }
    if (result != SQLITE_DONE)
        throw std::runtime_error("P6 independent snapshot row read failed: " + sql);
    return rows;
}
} // namespace detail

inline std::string logicalSnapshot(const std::filesystem::path& path) {
    sqlite3* raw = nullptr;
    if (sqlite3_open_v2(path.c_str(), &raw, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (raw)
            sqlite3_close(raw);
        throw std::runtime_error("P6 snapshot cannot open saved database read-only");
    }
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database(raw, sqlite3_close);
    const auto tables = detail::query(
        database.get(),
        "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name", true);
    std::string result;
    for (const auto& table : tables) {
        auto rows = detail::query(database.get(), "SELECT * FROM " + detail::quotedIdentifier(table));
        std::sort(rows.begin(), rows.end());
        result += std::to_string(table.size()) + ':' + table + std::to_string(rows.size()) + ':';
        for (const auto& row : rows)
            result += std::to_string(row.size()) + ':' + row;
    }
    return result;
}

struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("deep-signal-p6-compare-" + std::to_string(suffix));
        if (!std::filesystem::create_directory(path))
            throw std::runtime_error("P6 unique temporary directory already exists");
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

inline std::string durableSnapshot(const GameState& state, const std::filesystem::path& path) {
    save::SaveGameRepository::save(path, state);
    return logicalSnapshot(path);
}
} // namespace deep::p6test
