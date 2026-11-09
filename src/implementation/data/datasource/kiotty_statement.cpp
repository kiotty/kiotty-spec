#include "implementation/data/datasource/kiotty_statement.h"

#include "implementation/data/datasource/kiotty_db_connection.h"

#include <sqlite3.h>

#include <limits>
#include <utility>

namespace kiotty::spec::implementation {

namespace {

constexpr char EMPTY_TEXT[] = "";

KiottySpecStatus statusOf(int code)
{
    if (code == SQLITE_OK) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return KIOTTY_SPEC_STATUS_DATABASE;
}

}

Statement::Statement(DBConnection& connection, Span<const char> sql)
{
    if (sql.length() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        _status = KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED;
        return;
    }
    const int length = static_cast<int>(sql.length());
    _status = statusOf(sqlite3_prepare_v2(connection._database, sql.data(), length, &_statement, nullptr));
}

Statement::~Statement() { sqlite3_finalize(_statement); }

Statement::Statement(Statement&& other) noexcept : _statement(other._statement), _status(other._status)
{
    other._statement = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept
{
    std::swap(_statement, other._statement);
    std::swap(_status, other._status);
    return *this;
}

KiottySpecStatus Statement::bind(int index, int64_t value)
{
    return keep(statusOf(sqlite3_bind_int64(_statement, index, value)));
}

KiottySpecStatus Statement::bind(int index, double value)
{
    return keep(statusOf(sqlite3_bind_double(_statement, index, value)));
}

KiottySpecStatus Statement::bind(int index, Span<const char> value)
{
    const size_t maximum = static_cast<size_t>(std::numeric_limits<int>::max());
    if (value.length() > maximum) { return keep(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }
    const int length = static_cast<int>(value.length());
    const char* const text = value.data() != nullptr ? value.data() : EMPTY_TEXT;
    return keep(statusOf(sqlite3_bind_text(_statement, index, text, length, SQLITE_TRANSIENT)));
}

KiottySpecStatus Statement::bind(int index, std::nullptr_t)
{
    return keep(statusOf(sqlite3_bind_null(_statement, index)));
}

Result<bool, KiottySpecStatus> Statement::step()
{
    if (_status != KIOTTY_SPEC_STATUS_SUCCESS) { return Result<bool, KiottySpecStatus>::failure(_status); }
    const int code = sqlite3_step(_statement);
    if (code == SQLITE_ROW) { return Result<bool, KiottySpecStatus>::success(true); }
    if (code == SQLITE_DONE) { return Result<bool, KiottySpecStatus>::success(false); }
    return Result<bool, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_DATABASE);
}

KiottySpecStatus Statement::execute()
{
    Result<bool, KiottySpecStatus> stepped = step();
    while (stepped.value) { stepped = step(); }
    return stepped.code;
}

KiottySpecStatus Statement::keep(KiottySpecStatus status)
{
    if (_status == KIOTTY_SPEC_STATUS_SUCCESS) { _status = status; }
    return status;
}

int64_t Statement::insertedRowId() const { return sqlite3_last_insert_rowid(sqlite3_db_handle(_statement)); }

int64_t Statement::changedRowCount() const { return sqlite3_changes64(sqlite3_db_handle(_statement)); }

}
