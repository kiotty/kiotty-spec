#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_failure.h"
#include "implementation/data/repository/kiotty_checkpoint_writer.h"
#include "implementation/domain/entity/kiotty_item_records.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

class CheckpointStore
{
public:
    CheckpointStore(entity::Failure& failure, size_t capacity) : _failure(failure), _records(capacity), _exists(capacity, 0) {}

    void clear();
    KiottySpecStatus put(uint32_t sequence, const entity::CheckpointRecord& record);

    uint32_t latest() const { return _latest; }
    Result<const entity::CheckpointRecord*, KiottySpecStatus> get(uint32_t sequence) const;
    const entity::CheckpointRecord* at(uint32_t sequence) const;

    CheckpointWriter writer() { return CheckpointWriter(_failure, _records, _exists, _latest); }

private:
    entity::Failure& _failure;
    std::vector<entity::CheckpointRecord> _records;
    std::vector<uint8_t> _exists;
    uint32_t _latest {0};
};

}
