#include "implementation/core/database.h"

#include "implementation/core/error.h"

#include <sqlite3.h>

namespace kiotty::core {

namespace {

[[noreturn]] void failSqlite(sqlite3 *db, std::string_view what) {
    std::string message(what);
    message += ": ";
    message += db != nullptr ? sqlite3_errmsg(db) : "unknown sqlite error";
    fail(KiottySpecStatus_Db, message);
}

} // namespace

// ---------------------------------------------------------------------------
// Statement
// ---------------------------------------------------------------------------

Statement::Statement(sqlite3 *db, std::string_view sql) : db_(db), stmt_(nullptr) {
    if (sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt_, nullptr) != SQLITE_OK) {
        failSqlite(db_, std::string("prepare failed [") + std::string(sql) + "]");
    }
}

Statement::~Statement() {
    if (stmt_ != nullptr) {
        sqlite3_finalize(stmt_);
    }
}

Statement::Statement(Statement &&other) noexcept : db_(other.db_), stmt_(other.stmt_) {
    other.stmt_ = nullptr;
}

Statement &Statement::bindOne(int index, int64_t value) {
    if (sqlite3_bind_int64(stmt_, index, value) != SQLITE_OK) {
        failSqlite(db_, "bind int");
    }
    return *this;
}

Statement &Statement::bindOne(int index, double value) {
    if (sqlite3_bind_double(stmt_, index, value) != SQLITE_OK) {
        failSqlite(db_, "bind double");
    }
    return *this;
}

Statement &Statement::bindOne(int index, std::string_view value) {
    if (sqlite3_bind_text(stmt_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK) {
        failSqlite(db_, "bind text");
    }
    return *this;
}

Statement &Statement::bindOne(int index, const char *value) {
    if (value == nullptr) {
        return bindOne(index, nullptr);
    }
    return bindOne(index, std::string_view(value));
}

Statement &Statement::bindOne(int index, std::nullptr_t) {
    if (sqlite3_bind_null(stmt_, index) != SQLITE_OK) {
        failSqlite(db_, "bind null");
    }
    return *this;
}

Statement &Statement::bindOne(int index, const std::optional<std::string> &value) {
    return value ? bindOne(index, std::string_view(*value)) : bindOne(index, nullptr);
}

Statement &Statement::bindOne(int index, const std::optional<int64_t> &value) {
    return value ? bindOne(index, *value) : bindOne(index, nullptr);
}

bool Statement::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    failSqlite(db_, "step");
}

void Statement::run() {
    while (step()) {
    }
}

void Statement::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

int Statement::columnCount() const { return sqlite3_column_count(stmt_); }

std::string Statement::columnName(int column) const {
    const char *name = sqlite3_column_name(stmt_, column);
    return name != nullptr ? name : "";
}

ColumnType Statement::columnType(int column) const {
    switch (sqlite3_column_type(stmt_, column)) {
    case SQLITE_INTEGER: return ColumnType::Integer;
    case SQLITE_FLOAT: return ColumnType::Float;
    case SQLITE_TEXT: return ColumnType::Text;
    case SQLITE_BLOB: return ColumnType::Blob;
    default: return ColumnType::Null;
    }
}

bool Statement::isNull(int column) const { return sqlite3_column_type(stmt_, column) == SQLITE_NULL; }

int64_t Statement::integer(int column) const { return sqlite3_column_int64(stmt_, column); }

double Statement::real(int column) const { return sqlite3_column_double(stmt_, column); }

std::string Statement::text(int column) const {
    const auto *data = reinterpret_cast<const char *>(sqlite3_column_text(stmt_, column));
    const int size = sqlite3_column_bytes(stmt_, column);
    return data != nullptr ? std::string(data, static_cast<size_t>(size)) : std::string();
}

std::optional<std::string> Statement::optText(int column) const {
    if (isNull(column)) {
        return std::nullopt;
    }
    return text(column);
}

std::optional<int64_t> Statement::optInteger(int column) const {
    if (isNull(column)) {
        return std::nullopt;
    }
    return integer(column);
}

bool Statement::readOnly() const { return sqlite3_stmt_readonly(stmt_) != 0; }

// ---------------------------------------------------------------------------
// Database
// ---------------------------------------------------------------------------

Database::Database(const std::string &utf8Path) {
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    if (sqlite3_open_v2(utf8Path.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
        std::string message = "cannot open database " + utf8Path;
        if (db_ != nullptr) {
            message += ": ";
            message += sqlite3_errmsg(db_);
            sqlite3_close(db_);
            db_ = nullptr;
        }
        fail(KiottySpecStatus_Io, message);
    }
    sqlite3_busy_timeout(db_, 5000);
    exec("PRAGMA foreign_keys = OFF; PRAGMA journal_mode = WAL;");
}

Database::~Database() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
    }
}

void Database::exec(std::string_view sql) {
    char *error = nullptr;
    const std::string copy(sql);
    if (sqlite3_exec(db_, copy.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = "exec failed: ";
        message += error != nullptr ? error : "unknown";
        sqlite3_free(error);
        fail(KiottySpecStatus_Db, message);
    }
}

Statement Database::prepare(std::string_view sql) { return Statement(db_, sql); }

int64_t Database::lastInsertRowId() const { return sqlite3_last_insert_rowid(db_); }

int64_t Database::changes() const { return sqlite3_changes64(db_); }

// ---------------------------------------------------------------------------
// Savepoint
// ---------------------------------------------------------------------------

Savepoint::Savepoint(Database &db) : db_(db) { db_.exec("SAVEPOINT kiotty_sp"); }

Savepoint::~Savepoint() {
    if (!done_) {
        try {
            db_.exec("ROLLBACK TO kiotty_sp; RELEASE kiotty_sp;");
        } catch (...) {
            // 소멸자에서는 예외를 삼킨다.
        }
    }
}

void Savepoint::commit() {
    db_.exec("RELEASE kiotty_sp");
    done_ = true;
}

} // namespace kiotty::core
