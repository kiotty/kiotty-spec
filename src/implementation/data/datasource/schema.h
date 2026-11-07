#pragma once

#include "kiotty_spec.h"
#include "implementation/core/result.h"
#include "implementation/core/span.h"
#include "implementation/data/datasource/storage.h"

#include <cstdint>
#include <string_view>

namespace kiotty::spec::implementation {

inline constexpr int32_t SCHEMA_VERSION = 1;

struct VersionedTable {
    std::string_view name;
    std::string_view promote_sql;
    std::string_view count_pending_sql;
    std::string_view keep_latest_sql;
    std::string_view delete_older_sql;
    std::string_view delete_removed_sql;
};

struct OrderedTable {
    std::string_view name;
    std::string_view key_order;
};

Span<const VersionedTable> versionedTables();
Span<const OrderedTable> orderedTables();

KiottySpecStatus ensure(Storage& storage);
Result<int32_t, KiottySpecStatus> version(Storage& storage);

}
