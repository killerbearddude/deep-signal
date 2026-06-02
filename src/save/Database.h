#pragma once

// Provides thin RAII wrappers around the SQLite C API for the save layer.
// The simulation core never includes this file; SQLite ownership, statement
// finalization, and transaction rollback are isolated behind these classes.

#include <cstdint>
#include <filesystem>
#include <sqlite3.h>
#include <string>
#include <string_view>

namespace deep::save {

// Owns one SQLite connection. The connection enables foreign-key enforcement at
// construction time so every repository operation uses the same integrity rules.
class Database {
public:
    // Opens or creates the database at path. Throws std::runtime_error if SQLite
    // cannot open the file or enable required connection settings.
    explicit Database(const std::filesystem::path& path);

    // Closes the SQLite connection. Destructors must not throw; any close error
    // is intentionally ignored because statement errors are reported earlier.
    ~Database() noexcept;

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Executes SQL that has no bound values, such as schema creation or table
    // clearing. All value-bearing SQL must use Statement and parameter binding.
    void execute(std::string_view sql);

    // Returns the underlying handle for Statement construction inside this layer.
    [[nodiscard]] sqlite3* handle() noexcept;

private:
    sqlite3* db_ = nullptr;
};

// Owns one prepared SQLite statement. Values are always supplied through bind*
// functions so repository code does not concatenate data into SQL strings.
class Statement {
public:
    // Prepares sql against db. The SQL may contain positional parameters using
    // SQLite's '?' syntax and must remain valid only for this constructor call.
    Statement(Database& db, std::string_view sql);

    // Finalizes the prepared statement. SQLite finalization is non-throwing here
    // because execution errors are raised from step/execute at the call site.
    ~Statement() noexcept;

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    // Binds NULL to a 1-based SQLite parameter index.
    void bindNull(int index);

    // Binds an integral value to a 1-based SQLite parameter index.
    void bindInt64(int index, std::int64_t value);

    // Binds a floating-point value to a 1-based SQLite parameter index.
    void bindDouble(int index, double value);

    // Binds UTF-8 text to a 1-based SQLite parameter index. SQLite copies the
    // supplied bytes so callers may pass temporary strings safely.
    void bindText(int index, std::string_view value);

    // Advances the statement. Returns true for SQLITE_ROW and false for
    // SQLITE_DONE. Throws on any other SQLite result code.
    [[nodiscard]] bool step();

    // Executes a non-query statement and verifies that it does not produce rows.
    void execute();

    // Resets the statement so it can be reused with another set of bound values.
    void reset();

    // Clears all previously bound values. Call this with reset before reuse.
    void clearBindings();

    // Reads a signed 64-bit integer from the current row at a zero-based column.
    [[nodiscard]] std::int64_t columnInt64(int column) const;

    // Reads a double from the current row at a zero-based column.
    [[nodiscard]] double columnDouble(int column) const;

    // Reads UTF-8 text from the current row at a zero-based column.
    [[nodiscard]] std::string columnText(int column) const;

    // Returns true when the current row column is SQL NULL.
    [[nodiscard]] bool columnIsNull(int column) const noexcept;

private:
    Database& db_;
    sqlite3_stmt* stmt_ = nullptr;
};

// RAII transaction helper. If commit() is not called, destruction rolls back the
// transaction to avoid leaving partial saves or partial loads visible.
class Transaction {
public:
    enum class Mode {
        Read,
        Write
    };

    // Starts a read or write transaction. Write mode uses BEGIN IMMEDIATE so a
    // full-save operation obtains the write lock before clearing tables.
    Transaction(Database& db, Mode mode);

    // Rolls back an uncommitted transaction. Rollback failures are ignored here
    // because throwing from a destructor would terminate the process.
    ~Transaction() noexcept;

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    // Commits the transaction. Throws if SQLite rejects the commit.
    void commit();

private:
    Database& db_;
    bool committed_ = false;
};

} // namespace deep::save
