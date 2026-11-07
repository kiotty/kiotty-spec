#pragma once

#include "implementation/data/datasource/storage.h"
#include "implementation/data/repository/store_result.h"

#include <cstdint>
#include <string_view>

namespace kiotty::spec::implementation {

class CheckpointStore {
public:
    explicit CheckpointStore(Storage &storage) : _storage(storage) {}

    SequenceResult create(int32_t step, std::string_view note, KiottySpecCheckpointPurpose purpose);
    OptionalCheckpointResult find(int64_t seq);
    CheckpointResult get(int64_t seq);
    OptionalCheckpointResult latest();
    OptionalCheckpointResult lastConfirmed(int32_t stepOr0);
    CheckpointListResult list(int32_t stepOr0);
    KiottySpecStatus markConfirmed(int64_t seq);
    SequenceResult resolve(std::string_view cpId);
    CountResult pendingWriteCount();
    CountResult prune(int64_t baseSeq);

private:
    SequenceResult insert(int32_t step, std::string_view note, KiottySpecCheckpointPurpose purpose);
    KiottySpecStatus promotePending(int64_t seq);
    CountResult pruneTables(int64_t baseSeq);
    CountResult pruneHistory(int64_t baseSeq);
    CountResult change(std::string_view sql, int64_t first);
    CountResult change(std::string_view sql, int64_t first, int64_t second);
    OptionalCheckpointResult first(std::string_view sql, int64_t value);
    OptionalCheckpointResult single(Query &query);

    Storage &_storage;
};

}
