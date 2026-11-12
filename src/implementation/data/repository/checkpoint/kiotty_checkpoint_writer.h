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

struct CheckpointColumns {
    std::vector<entity::DescriptionText>& descriptions;
    std::vector<entity::CheckpointRecord>& records;
    std::vector<uint8_t>& exists;
};

static_assert(sizeof(CheckpointColumns) == 24, "unexpected padding");

class CheckpointWriter
{
public:
    CheckpointWriter(entity::Failure& failure, const CheckpointColumns& columns, uint32_t& latest)
        : _failure(failure), _descriptions(columns.descriptions), _records(columns.records),
          _exists(columns.exists), _latest(latest)
    {
    }

    Result<uint32_t, KiottySpecStatus> create(entity::Step step, Span<const char> description,
                                              int64_t created_at);
    KiottySpecStatus setState(uint32_t sequence, entity::CheckpointState state);
    Result<size_t, KiottySpecStatus> dropBefore(uint32_t base);

private:
    Span<entity::CheckpointRecord> viewRecords() const
    { return viewVector(_records); }
    Span<uint8_t> viewFlags() const { return viewVector(_exists); }

    entity::Failure& _failure;
    std::vector<entity::DescriptionText>& _descriptions;
    std::vector<entity::CheckpointRecord>& _records;
    std::vector<uint8_t>& _exists;
    uint32_t& _latest;
};

}
