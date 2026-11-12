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

KiottySpecStatus CheckpointStore::put(uint32_t sequence, const CheckpointRecord& record,
                                      const entity::DescriptionText& description)
{
    const Span<CheckpointRecord> records = viewVector(_records);
    const Span<entity::DescriptionText> descriptions = viewVector(_descriptions);
    const Result<entity::DescriptionText&, KiottySpecStatus> text = descriptions.at(sequence - 1);
    const Span<uint8_t> exists = viewVector(_exists);
    const Result<CheckpointRecord&, KiottySpecStatus> slot = records.at(sequence - 1);
    const Result<uint8_t&, KiottySpecStatus> flag = exists.at(sequence - 1);
    const bool is_out_of_range = !slot || !flag || !text;
    if (is_out_of_range) {
        return _failure.raise(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED,
                              KIOTTY_SPEC_SUBJECT_CONFIGURATION, literalText("max_checkpoints"));
    }
    const bool is_taken = *flag.value != 0;
    if (is_taken) {
        return _failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, KIOTTY_SPEC_SUBJECT_DUPLICATE_LINE);
    }
    *slot.value = record;
    *text.value = description;
    *flag.value = 1;
    _latest = std::max(_latest, sequence);
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<const CheckpointRecord&, KiottySpecStatus> CheckpointStore::get(uint32_t sequence) const
{
    using RecordResult = Result<const CheckpointRecord&, KiottySpecStatus>;
    const Span<const CheckpointRecord> records = viewVector(_records);
    const Span<const uint8_t> exists = viewVector(_exists);
    const RecordResult record = records.at(sequence - 1);
    const Result<const uint8_t&, KiottySpecStatus> flag = exists.at(sequence - 1);
    const bool is_present = record && flag && *flag.value != 0;
    if (!is_present) { return RecordResult::failure(KIOTTY_SPEC_STATUS_NOT_FOUND); }
    return record;
}

entity::DescriptionText CheckpointStore::describe(uint32_t sequence) const
{
    const Span<const entity::DescriptionText> descriptions = viewVector(_descriptions);
    const Result<const entity::DescriptionText&, KiottySpecStatus> text =
        descriptions.at(sequence - 1);
    if (!text) { return entity::DescriptionText {}; }
    return *text.value;
}

}
