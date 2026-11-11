#include "implementation/data/repository/kiotty_checkpoint_store.h"

#include "implementation/core/kiotty_text_view.h"

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
    if (sequence < 1 || sequence > _records.size()) {
        return _failure.raise(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED,
                              KIOTTY_SPEC_SUBJECT_CONFIGURATION, literalText("max_checkpoints"));
    }
    if (_exists[sequence - 1] != 0) {
        return _failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, KIOTTY_SPEC_SUBJECT_DUPLICATE_LINE);
    }
    _records[sequence - 1] = record;
    _exists[sequence - 1] = 1;
    if (sequence > _latest) { _latest = sequence; }
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
    if (sequence == 0 || sequence > _records.size() || _exists[sequence - 1] == 0) {
        return nullptr;
    }
    return &_records[sequence - 1];
}

}
