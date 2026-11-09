#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_query.h"

#include <cstdint>

struct sqlite3;

namespace kiotty::spec::implementation {

class Connection
{
public:
    Connection() = default;
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    KiottySpecStatus open(Span<const char> path);
    void close();
    bool isOpen() const { return _database != nullptr; }
    KiottySpecStatus execute(Span<const char> sql);
    Result<Query, KiottySpecStatus> prepare(Span<const char> sql);
    int64_t insertedRowId() const;
    int64_t changedRowCount() const;
    Span<const char> message() const;

private:
    sqlite3* _database {nullptr};
    int _open_code {0};
};

}
