#pragma once

// Responsibility: contain SQLite handle ownership and errors within save/.
// Database owns a connection; Statement and Transaction borrow it and must be
// destroyed first. These wrappers do not validate domain data or choose schema
// compatibility policy. Callers must serialize use of a connection and its
// statements; this layer provides no application-level concurrency coordination.

#include <cstdint>
#include <filesystem>
#include <sqlite3.h>
#include <string>
#include <string_view>

namespace deep::save {

// Owns one SQLite connection with foreign-key enforcement requested at opening.
// Lock contention and other SQLite failures surface as exceptions; this wrapper
// does not configure busy retries or attempt recovery.
class Database {
public:
    // Opens or creates the database at path. Throws std::runtime_error if opening
    // or executing the connection setup SQL fails.
    explicit Database(const std::filesystem::path& path);

    // Attempts to close the connection without throwing. Outstanding statements
    // can prevent closing, so their shorter lifetime is a caller obligation.
    ~Database() noexcept;

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Executes SQL that has no bound values, such as schema creation or table
    // clearing. All value-bearing SQL must use Statement and parameter binding.
    void execute(std::string_view sql);

    // Borrows the handle for save-layer operations; the caller must not close it
    // or retain it beyond this Database's lifetime.
    [[nodiscard]] sqlite3* handle() noexcept;

private:
    sqlite3* db_ = nullptr;
};

// Owns one prepared SQLite statement and borrows its Database for its lifetime.
// Bound values keep data separate from SQL. Column access requires a successful
// step() returning true; a later step/reset invalidates the current row.
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

    // Uses SQLite's integer conversion at a zero-based column. This does not
    // verify the stored type or reject fractional/text input before conversion.
    [[nodiscard]] std::int64_t columnInt64(int column) const;

    // Uses SQLite's double conversion; storage type and finiteness are not
    // checked here. Domain validation happens after repository reconstruction.
    [[nodiscard]] double columnDouble(int column) const;

    // Copies SQLite's text representation into an owned string. NULL yields an
    // empty string; use columnIsNull when that distinction matters.
    [[nodiscard]] std::string columnText(int column) const;

    // Returns true when the current row column is SQL NULL.
    [[nodiscard]] bool columnIsNull(int column) const noexcept;

private:
    Database& db_;
    sqlite3_stmt* stmt_ = nullptr;
};

// Borrows a Database and owns one transaction on that connection. Nesting is not
// supported. Until commit succeeds, destruction attempts rollback; it cannot
// undo schema or data changes performed before the transaction began.
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

    // Commits the transaction. A rejected commit throws and leaves this helper
    // uncommitted so destruction still attempts rollback.
    void commit();

private:
    Database& db_;
    bool committed_ = false;
};

} // namespace deep::save
