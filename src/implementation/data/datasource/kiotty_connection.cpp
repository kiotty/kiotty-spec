#include "implementation/data/datasource/kiotty_connection.h"

#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_text_view.h"

#include <sqlite3.h>

#include <limits>

namespace kiotty::spec::implementation {

namespace {

constexpr size_t MAX_PATH_LENGTH = 1000;
constexpr size_t MAX_MESSAGE_LENGTH = 1024;
constexpr int BUSY_TIMEOUT_MILLISECONDS = 5000;
constexpr char OPEN_PRAGMAS[] = "PRAGMA foreign_keys = OFF; PRAGMA journal_mode = WAL;";

bool fitsInt(Span<const char> text)
{
    return text.length() <= static_cast<size_t>(std::numeric_limits<int>::max());
}

}

Connection::~Connection() { sqlite3_close(_database); }

void Connection::close()
{
    sqlite3_close(_database);
    _database = nullptr;
}

KiottySpecStatus Connection::open(Span<const char> path)
{
    FixedString<MAX_PATH_LENGTH> terminated;
    const KiottySpecStatus copied = terminated.assign(path);
    if (copied != KIOTTY_SPEC_STATUS_SUCCESS) { return copied; }
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    _open_code = sqlite3_open_v2(terminated.c_str(), &_database, flags, nullptr);
    if (_open_code != SQLITE_OK) {
        sqlite3_close(_database);
        _database = nullptr;
        return KIOTTY_SPEC_STATUS_INPUT_OUTPUT;
    }
    sqlite3_busy_timeout(_database, BUSY_TIMEOUT_MILLISECONDS);
    return execute(literalText(OPEN_PRAGMAS));
}

KiottySpecStatus Connection::execute(Span<const char> sql)
{
    if (!fitsInt(sql)) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    const char* position = sql.data();
    const char* const end = sql.data() + sql.length();
    while (position < end) {
        sqlite3_stmt* statement = nullptr;
        const int length = static_cast<int>(end - position);
        const int code = sqlite3_prepare_v2(_database, position, length, &statement, &position);
        if (code != SQLITE_OK) { return KIOTTY_SPEC_STATUS_DATABASE; }
        if (statement == nullptr) { return KIOTTY_SPEC_STATUS_SUCCESS; }
        Query query(statement);
        const KiottySpecStatus status = query.run();
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<Query, KiottySpecStatus> Connection::prepare(Span<const char> sql)
{
    using QueryResult = Result<Query, KiottySpecStatus>;
    if (!fitsInt(sql)) { return QueryResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }
    sqlite3_stmt* statement = nullptr;
    const int length = static_cast<int>(sql.length());
    const int code = sqlite3_prepare_v2(_database, sql.data(), length, &statement, nullptr);
    if (code != SQLITE_OK) { return QueryResult::failure(KIOTTY_SPEC_STATUS_DATABASE); }
    return QueryResult::success(Query(statement));
}

int64_t Connection::insertedRowId() const { return sqlite3_last_insert_rowid(_database); }

int64_t Connection::changedRowCount() const { return sqlite3_changes64(_database); }

Span<const char> Connection::message() const
{
    if (_database == nullptr) { return boundedText(sqlite3_errstr(_open_code), MAX_MESSAGE_LENGTH); }
    return boundedText(sqlite3_errmsg(_database), MAX_MESSAGE_LENGTH);
}

}
