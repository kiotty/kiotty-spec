#pragma once

#include "implementation/data/datasource/storage.h"
#include "implementation/data/repository/store_result.h"

#include <cstdint>
#include <string_view>

namespace kiotty::spec::implementation {

class VersionHistory {
public:
    explicit VersionHistory(Storage &storage) : _storage(storage) {}

    CountResult pendingCount();
    CountResult prune(int64_t baseSeq);

private:
    CountResult pruneTables(int64_t baseSeq);
    CountResult pruneHistory(int64_t baseSeq);
    CountResult change(std::string_view sql, int64_t first);

    Storage &_storage;
};

}
