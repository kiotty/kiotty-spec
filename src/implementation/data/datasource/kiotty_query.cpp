#include "implementation/data/datasource/kiotty_query.h"

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

Query::~Query() { sqlite3_finalize(_statement); }

Query::Query(Query&& other) noexcept : _statement(other._statement), _bind_status(other._bind_status)
{
    other._statement = nullptr;
}

Query& Query::operator=(Query&& other) noexcept
{
    std::swap(_statement, other._statement);
    std::swap(_bind_status, other._bind_status);
    return *this;
}

KiottySpecStatus Query::bind(int index, int64_t value)
{
    return keep(statusOf(sqlite3_bind_int64(_statement, index, value)));
}

KiottySpecStatus Query::bind(int index, double value)
{
    return keep(statusOf(sqlite3_bind_double(_statement, index, value)));
}

KiottySpecStatus Query::bind(int index, Span<const char> value)
{
    const size_t maximum = static_cast<size_t>(std::numeric_limits<int>::max());
    if (value.length() > maximum) { return keep(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED); }
    const int length = static_cast<int>(value.length());
    const char* const text = value.data() != nullptr ? value.data() : EMPTY_TEXT;
    return keep(statusOf(sqlite3_bind_text(_statement, index, text, length, SQLITE_TRANSIENT)));
}

KiottySpecStatus Query::bind(int index, std::nullptr_t)
{
    return keep(statusOf(sqlite3_bind_null(_statement, index)));
}

Result<bool, KiottySpecStatus> Query::step()
{
    if (_bind_status != KIOTTY_SPEC_STATUS_SUCCESS) { return Result<bool, KiottySpecStatus>::failure(_bind_status); }
    const int code = sqlite3_step(_statement);
    if (code == SQLITE_ROW) { return Result<bool, KiottySpecStatus>::success(true); }
    if (code == SQLITE_DONE) { return Result<bool, KiottySpecStatus>::success(false); }
    return Result<bool, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_DATABASE);
}

KiottySpecStatus Query::run()
{
    Result<bool, KiottySpecStatus> stepped = step();
    while (stepped.value) { stepped = step(); }
    return stepped.code;
}

KiottySpecStatus Query::keep(KiottySpecStatus status)
{
    if (_bind_status == KIOTTY_SPEC_STATUS_SUCCESS) { _bind_status = status; }
    return status;
}

bool Query::isReadOnly() const { return sqlite3_stmt_readonly(_statement) != 0; }

}
