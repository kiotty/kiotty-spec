#include "implementation/data/repository/kiotty_checkpoint_writer.h"

#include "implementation/core/kiotty_text_view.h"

#include <algorithm>

namespace kiotty::spec::implementation {

namespace {

using entity::CheckpointHandle;
using entity::CheckpointRecord;
using entity::CheckpointState;

using RecordResult = Result<CheckpointRecord&, KiottySpecStatus>;

uint32_t advanceSequence(uint32_t latest);
RecordResult findRecord(Span<CheckpointRecord> records, Span<const uint8_t> exists,
                        uint32_t sequence);
size_t clearBefore(Span<uint8_t> exists, uint32_t base);

}

Result<uint32_t, KiottySpecStatus>
CheckpointWriter::create(entity::Step step, Span<const char> description, int64_t created_at)
{
    using SequenceResult = Result<uint32_t, KiottySpecStatus>;
    const uint32_t sequence = advanceSequence(_latest);
    const Span<CheckpointRecord> records = viewRecords();
    const Span<uint8_t> exists = viewFlags();
    const RecordResult slot = records.at(sequence - 1);
    const Result<uint8_t&, KiottySpecStatus> flag = exists.at(sequence - 1);
    const Span<entity::DescriptionText> descriptions = viewVector(_descriptions);
    const auto text = descriptions.at(sequence - 1);
    const bool is_out_of_range = !slot || !flag || !text;
    if (is_out_of_range) {
        return SequenceResult::failure(_failure.raise(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED,
                                                      KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                                                      literalText("max_checkpoints")));
    }
    CheckpointRecord& record = *slot.value;
    record = CheckpointRecord{};
    record.created_at = created_at;
    record.previous = CheckpointHandle{_latest, 0};
    record.step = step;
    record.state = CheckpointState::STARTED;
    (*text.value).assign(description);
    *flag.value = 1;
    _latest = sequence;
    return SequenceResult::success(sequence);
}

KiottySpecStatus CheckpointWriter::setState(uint32_t sequence, CheckpointState state)
{
    const Span<CheckpointRecord> records = viewRecords();
    const Span<const uint8_t> exists = viewFlags();
    const RecordResult found = findRecord(records, exists, sequence);
    if (!found) { return KIOTTY_SPEC_STATUS_NOT_FOUND; }
    CheckpointRecord& record = *found.value;
    record.state = state;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<size_t, KiottySpecStatus> CheckpointWriter::dropBefore(uint32_t base)
{
    const Span<uint8_t> exists = viewFlags();
    const size_t count = clearBefore(exists, base);
    const Span<CheckpointRecord> records = viewRecords();
    const RecordResult found = records.at(base - 1);
    if (found) {
        CheckpointRecord& record = *found.value;
        record.previous = entity::INVALID_CHECKPOINT_HANDLE;
    }
    return Result<size_t, KiottySpecStatus>::success(count);
}

namespace {

uint32_t advanceSequence(uint32_t latest) { return latest + 1; }

RecordResult findRecord(Span<CheckpointRecord> records, Span<const uint8_t> exists,
                        uint32_t sequence)
{
    const RecordResult record = records.at(sequence - 1);
    const Result<const uint8_t&, KiottySpecStatus> flag = exists.at(sequence - 1);
    const bool is_present = record && flag && *flag.value != 0;
    return is_present ? record : RecordResult::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
}

size_t clearBefore(Span<uint8_t> exists, uint32_t base)
{
    const size_t first_kept = base > 0 ? static_cast<size_t>(base) - 1 : 0;
    const size_t limit = std::min(first_kept, exists.length());
    size_t count = 0;
    for (size_t index = 0; index < limit; ++index) {
        count += exists[index];
        exists[index] = 0;
    }
    return count;
}

}

}
