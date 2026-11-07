#pragma once

#include "kiotty_spec.h"
#include "implementation/core/result.h"
#include "implementation/core/span.h"
#include "implementation/data/datasource/row.h"

#include <cstddef>
#include <cstdint>

struct sqlite3_stmt;

namespace kiotty::spec::implementation {

class Query
{
public:
    Query() = default;
    explicit Query(sqlite3_stmt* statement) : _statement(statement) {}
    ~Query();

    Query(const Query&) = delete;
    Query& operator=(const Query&) = delete;
    Query(Query&& other) noexcept;
    Query& operator=(Query&& other) noexcept;

    KiottySpecStatus bind(int index, int64_t value);
    KiottySpecStatus bind(int index, double value);
    KiottySpecStatus bind(int index, Span<const char> value);
    KiottySpecStatus bind(int index, std::nullptr_t);
    Result<bool, KiottySpecStatus> step();
    KiottySpecStatus run();
    Row row() const { return Row(_statement); }
    bool isReadOnly() const;

private:
    KiottySpecStatus keep(KiottySpecStatus status);

    sqlite3_stmt* _statement {nullptr};
    KiottySpecStatus _bind_status {KIOTTY_SPEC_STATUS_SUCCESS};
};

}
