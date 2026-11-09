#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_failure.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_db_connection.h"
#include "implementation/data/datasource/kiotty_statement.h"

#include <cstdint>
#include <initializer_list>

namespace kiotty::spec::implementation {

class Storage
{
public:
    Storage(DBConnection& connection, Failure& failure) : _connection(connection), _failure(failure) {}

    // 실패하면 이유를 남긴다.
    Result<Statement, KiottySpecStatus> prepare(Span<const char> sql);
    KiottySpecStatus executeScript(Span<const char> sql);
    KiottySpecStatus check(KiottySpecStatus status);
    KiottySpecStatus reject(KiottySpecStatus status, std::initializer_list<Span<const char>> parts);
    DBConnection& connection() { return _connection; }

private:
    DBConnection& _connection;
    Failure& _failure;
};

}
