#include "implementation/data/repository/kiotty_checkpoint_writer.h"

#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

using entity::CheckpointHandle;
using entity::CheckpointRecord;
using entity::CheckpointState;

}

Result<uint32_t, KiottySpecStatus> CheckpointWriter::create(entity::Step step, Span<const char> description, int64_t created_at)
{
    using SequenceResult = Result<uint32_t, KiottySpecStatus>;
    const uint32_t sequence = _latest + 1;
    if (sequence > _records.size()) {
        return SequenceResult::failure(_failure.raise(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, KIOTTY_SPEC_SUBJECT_CONFIGURATION, literalText("max_checkpoints")));
    }
    CheckpointRecord record {};
    record.created_at = created_at;
    record.previous = CheckpointHandle{_latest, 0};
    record.step = step;
    record.state = CheckpointState::STARTED;
    record.description.assign(description);
    _records[sequence - 1] = record;
    _exists[sequence - 1] = 1;
    _latest = sequence;
    return SequenceResult::success(sequence);
}

KiottySpecStatus CheckpointWriter::setState(uint32_t sequence, CheckpointState state)
{
    if (sequence == 0 || sequence > _records.size() || _exists[sequence - 1] == 0) { return KIOTTY_SPEC_STATUS_NOT_FOUND; }
    _records[sequence - 1].state = state;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<size_t, KiottySpecStatus> CheckpointWriter::dropBefore(uint32_t base)
{
    size_t count = 0;
    for (uint32_t sequence = 1; sequence < base && sequence <= _records.size(); ++sequence) {
        count += _exists[sequence - 1];
        _exists[sequence - 1] = 0;
    }
    if (base >= 1 && base <= _records.size()) { _records[base - 1].previous = entity::INVALID_CHECKPOINT_HANDLE; }
    return Result<size_t, KiottySpecStatus>::success(count);
}

}
