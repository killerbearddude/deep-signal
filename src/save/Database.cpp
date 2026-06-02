#include "save/Database.h"

// Implements the SQLite RAII wrappers used by SaveGameRepository.
// All raw sqlite3 handles remain private to this layer to keep resource
// ownership explicit and prevent C API details from leaking into app or sim code.

#include <stdexcept>
#include <string>

namespace deep::save {
namespace {

// Builds a diagnostic message from the current SQLite connection state. The
// prefix describes the failed operation; sqlite3_errmsg supplies engine detail.
[[nodiscard]] std::runtime_error sqliteError(sqlite3* db, const std::string_view prefix) {
    std::string message{prefix};
    message += ": ";
    message += sqlite3_errmsg(db);
    return std::runtime_error{std::move(message)};
}

// Checks a SQLite return code for simple API calls. SQLITE_OK is the only success
// code for prepare/bind/exec setup calls handled by this helper.
void requireOk(sqlite3* db, const int rc, const std::string_view operation) {
    if (rc != SQLITE_OK) {
        throw sqliteError(db, operation);
    }
}

} // namespace

Database::Database(const std::filesystem::path& path) {
    const std::string utf8Path = path.string();
    const int rc = sqlite3_open(utf8Path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        sqlite3* failedDb = db_;
        db_ = nullptr;
        std::string message = "Failed to open SQLite database: ";
        message += failedDb != nullptr ? sqlite3_errmsg(failedDb) : "unknown error";
        if (failedDb != nullptr) {
            sqlite3_close(failedDb);
        }
        throw std::runtime_error{std::move(message)};
    }

    // Foreign keys are connection-local in SQLite, so enable them immediately for
    // every repository operation rather than relying on database file metadata.
    execute("PRAGMA foreign_keys = ON;");
}

Database::~Database() noexcept {
    if (db_ != nullptr) {
        (void)sqlite3_close(db_);
        db_ = nullptr;
    }
}

void Database::execute(const std::string_view sql) {
    char* errorMessage = nullptr;
    const std::string sqlText{sql};
    const int rc = sqlite3_exec(db_, sqlText.c_str(), nullptr, nullptr, &errorMessage);
    if (rc != SQLITE_OK) {
        std::string message = "SQLite exec failed: ";
        if (errorMessage != nullptr) {
            message += errorMessage;
            sqlite3_free(errorMessage);
        } else {
            message += sqlite3_errmsg(db_);
        }
        throw std::runtime_error{std::move(message)};
    }
}

sqlite3* Database::handle() noexcept {
    return db_;
}

Statement::Statement(Database& db, const std::string_view sql)
    : db_{db} {
    const std::string sqlText{sql};
    const int rc = sqlite3_prepare_v2(db_.handle(), sqlText.c_str(), -1, &stmt_, nullptr);
    requireOk(db_.handle(), rc, "Failed to prepare SQLite statement");
}

Statement::~Statement() noexcept {
    if (stmt_ != nullptr) {
        (void)sqlite3_finalize(stmt_);
        stmt_ = nullptr;
    }
}

void Statement::bindNull(const int index) {
    requireOk(db_.handle(), sqlite3_bind_null(stmt_, index), "Failed to bind NULL");
}

void Statement::bindInt64(const int index, const std::int64_t value) {
    requireOk(db_.handle(), sqlite3_bind_int64(stmt_, index, static_cast<sqlite3_int64>(value)), "Failed to bind integer");
}

void Statement::bindDouble(const int index, const double value) {
    requireOk(db_.handle(), sqlite3_bind_double(stmt_, index, value), "Failed to bind double");
}

void Statement::bindText(const int index, const std::string_view value) {
    requireOk(db_.handle(), sqlite3_bind_text(stmt_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT),
              "Failed to bind text");
}

bool Statement::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    throw sqliteError(db_.handle(), "Failed to step SQLite statement");
}

void Statement::execute() {
    if (step()) {
        throw std::runtime_error{"SQLite statement unexpectedly returned a row"};
    }
}

void Statement::reset() {
    const int rc = sqlite3_reset(stmt_);
    requireOk(db_.handle(), rc, "Failed to reset SQLite statement");
}

void Statement::clearBindings() {
    const int rc = sqlite3_clear_bindings(stmt_);
    requireOk(db_.handle(), rc, "Failed to clear SQLite bindings");
}

std::int64_t Statement::columnInt64(const int column) const {
    return static_cast<std::int64_t>(sqlite3_column_int64(stmt_, column));
}

double Statement::columnDouble(const int column) const {
    return sqlite3_column_double(stmt_, column);
}

std::string Statement::columnText(const int column) const {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, column));
    if (text == nullptr) {
        return {};
    }
    const int byteCount = sqlite3_column_bytes(stmt_, column);
    return std::string{text, static_cast<std::size_t>(byteCount)};
}

bool Statement::columnIsNull(const int column) const noexcept {
    return sqlite3_column_type(stmt_, column) == SQLITE_NULL;
}

Transaction::Transaction(Database& db, const Mode mode)
    : db_{db} {
    if (mode == Mode::Write) {
        db_.execute("BEGIN IMMEDIATE TRANSACTION;");
    } else {
        db_.execute("BEGIN TRANSACTION;");
    }
}

Transaction::~Transaction() noexcept {
    if (!committed_) {
        try {
            db_.execute("ROLLBACK;");
        } catch (...) {
            // Rollback failure cannot be reported from a destructor. The original
            // exception, if any, remains the meaningful error for callers.
        }
    }
}

void Transaction::commit() {
    db_.execute("COMMIT;");
    committed_ = true;
}

} // namespace deep::save
