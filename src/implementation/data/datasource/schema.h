#pragma once

#include "implementation/data/datasource/database.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace kiotty::spec::implementation {

inline constexpr int32_t kVersion = 1;

struct VersionedTable {
    std::string_view name;
    std::string_view keyColumns;
    bool hasRemoved;
};

const std::vector<VersionedTable> &versionedTables();
const std::vector<std::string_view> &allTables();
std::string_view primaryKeyOrder(std::string_view table);

void ensure(Database &db);
int32_t version(Database &db);

}
