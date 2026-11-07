#pragma once

#include "kiotty_spec.h"
#include "implementation/core/failure.h"
#include "implementation/core/result.h"
#include "implementation/core/span.h"
#include "implementation/data/datasource/connection.h"
#include "implementation/data/datasource/query.h"

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
