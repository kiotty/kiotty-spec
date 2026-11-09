#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_failure.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_connection.h"
#include "implementation/data/datasource/kiotty_query.h"

#include <cstdint>
#include <initializer_list>

namespace kiotty::spec::implementation {

class Storage
{
public:
    Storage(Connection& connection, Failure& failure) : _connection(connection), _failure(failure) {}

    Result<Query, KiottySpecStatus> prepare(Span<const char> sql);
    KiottySpecStatus execute(Span<const char> sql);
    KiottySpecStatus check(KiottySpecStatus status);
    KiottySpecStatus reject(KiottySpecStatus status, std::initializer_list<Span<const char>> parts);
    int64_t changedRowCount() const { return _connection.changedRowCount(); }
    int64_t insertedRowId() const { return _connection.insertedRowId(); }
    Connection& connection() { return _connection; }

private:
    Connection& _connection;
    Failure& _failure;
};

}
