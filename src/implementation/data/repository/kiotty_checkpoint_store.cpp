#include "implementation/data/repository/kiotty_checkpoint_store.h"

#include "implementation/core/kiotty_text_view.h"

#include <algorithm>

namespace kiotty::spec::implementation {

namespace {

using entity::CheckpointRecord;

}

void CheckpointStore::clear()
{
    for (auto& flag : _exists) { flag = 0; }
    _latest = 0;
}

KiottySpecStatus CheckpointStore::put(uint32_t sequence, const CheckpointRecord& record)
{
    const Span<CheckpointRecord> records(_records.data(), _records.size());
    const Span<uint8_t> exists(_exists.data(), _exists.size());
    const Result<CheckpointRecord&, KiottySpecStatus> slot = records.at(sequence - 1);
    const Result<uint8_t&, KiottySpecStatus> flag = exists.at(sequence - 1);
    const bool is_out_of_range = !slot || !flag;
    if (is_out_of_range) {
        return _failure.raise(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED,
                              KIOTTY_SPEC_SUBJECT_CONFIGURATION, literalText("max_checkpoints"));
    }
    const bool is_taken = *flag.value != 0;
    if (is_taken) {
        return _failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, KIOTTY_SPEC_SUBJECT_DUPLICATE_LINE);
    }
    *slot.value = record;
    *flag.value = 1;
    _latest = std::max(_latest, sequence);
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<const CheckpointRecord*, KiottySpecStatus> CheckpointStore::get(uint32_t sequence) const
{
    const CheckpointRecord* record = at(sequence);
    if (record == nullptr) {
        return Result<const CheckpointRecord*, KiottySpecStatus>::failure(
            KIOTTY_SPEC_STATUS_NOT_FOUND);
    }
    return Result<const CheckpointRecord*, KiottySpecStatus>::success(record);
}

const CheckpointRecord* CheckpointStore::at(uint32_t sequence) const
{
    const Span<const CheckpointRecord> records(_records.data(), _records.size());
    const Span<const uint8_t> exists(_exists.data(), _exists.size());
    const Result<const CheckpointRecord&, KiottySpecStatus> record = records.at(sequence - 1);
    const Result<const uint8_t&, KiottySpecStatus> flag = exists.at(sequence - 1);
    const bool is_present = record && flag && *flag.value != 0;
    return is_present ? record.value : nullptr;
}

}
