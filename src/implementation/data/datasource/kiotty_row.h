#pragma once

#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_column_type.h"

#include <cstdint>

struct sqlite3_stmt;

namespace kiotty::spec::implementation {

class Row
{
public:
    explicit Row(sqlite3_stmt* statement) : _statement(statement) {}

    int count() const;
    Span<const char> name(int column) const;
    ColumnType type(int column) const;
    bool isNull(int column) const;
    int64_t integer(int column) const;
    double real(int column) const;
    Span<const char> text(int column) const;

private:
    sqlite3_stmt* _statement;
};

}
