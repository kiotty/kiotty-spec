#include "implementation/data/datasource/row.h"

#include "implementation/core/text_view.h"

#include <sqlite3.h>

namespace kiotty::spec::implementation {

namespace {

constexpr size_t MAX_COLUMN_NAME_LENGTH = 256;

const char* asCharacters(const unsigned char* bytes)
{
    const void* address = bytes;
    return static_cast<const char*>(address);
}

}

int Row::count() const { return sqlite3_column_count(_statement); }

Span<const char> Row::name(int column) const
{
    return boundedText(sqlite3_column_name(_statement, column), MAX_COLUMN_NAME_LENGTH);
}

ColumnType Row::type(int column) const
{
    switch (sqlite3_column_type(_statement, column)) {
    case SQLITE_INTEGER: return ColumnType::INTEGER;
    case SQLITE_FLOAT: return ColumnType::FLOAT;
    case SQLITE_TEXT: return ColumnType::TEXT;
    case SQLITE_BLOB: return ColumnType::BLOB;
    default: return ColumnType::NULL_VALUE;
    }
}

bool Row::isNull(int column) const { return sqlite3_column_type(_statement, column) == SQLITE_NULL; }

int64_t Row::integer(int column) const { return sqlite3_column_int64(_statement, column); }

double Row::real(int column) const { return sqlite3_column_double(_statement, column); }

Span<const char> Row::text(int column) const
{
    const char* data = asCharacters(sqlite3_column_text(_statement, column));
    const int length = sqlite3_column_bytes(_statement, column);
    if (data == nullptr) { return Span<const char>(); }
    return Span<const char>(data, static_cast<size_t>(length));
}

}
