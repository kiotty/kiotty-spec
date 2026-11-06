#pragma once

#include "implementation/data/datasource/database.h"
#include "implementation/domain/entity/records.h"

#include <optional>
#include <string_view>
#include <vector>

namespace kiotty::spec::implementation {

class CheckpointStore {
public:
    explicit CheckpointStore(Database &db) : db_(db) {}

    int64_t create(int32_t step, std::string_view note, KiottySpecCheckpointPurpose purpose);
    std::optional<CheckpointRecord> find(int64_t seq);
    CheckpointRecord get(int64_t seq);
    std::optional<CheckpointRecord> latest();
    std::optional<CheckpointRecord> lastConfirmed(int32_t stepOr0);
    std::vector<CheckpointRecord> list(int32_t stepOr0);
    void markConfirmed(int64_t seq);

    int64_t resolve(std::string_view cpId);

    size_t pendingWriteCount();
    size_t prune(int64_t baseSeq);

private:
    Database &db_;
};

}
