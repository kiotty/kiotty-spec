#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_failure.h"
#include "implementation/domain/entity/kiotty_item_records.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

class CheckpointWriter
{
public:
    CheckpointWriter(entity::Failure& failure, std::vector<entity::CheckpointRecord>& records, std::vector<uint8_t>& exists, uint32_t& latest)
        : _failure(failure), _records(records), _exists(exists), _latest(latest)
    {
    }

    Result<uint32_t, KiottySpecStatus> create(entity::Step step, Span<const char> description, int64_t created_at);
    KiottySpecStatus setState(uint32_t sequence, entity::CheckpointState state);
    Result<size_t, KiottySpecStatus> dropBefore(uint32_t base);

private:
    entity::Failure& _failure;
    std::vector<entity::CheckpointRecord>& _records;
    std::vector<uint8_t>& _exists;
    uint32_t& _latest;
};

}
