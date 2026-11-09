#include "implementation/data/repository/kiotty_checkpoint_store.h"

#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

using entity::CheckpointHandle;
using entity::CheckpointRecord;
using entity::CheckpointState;

}

KiottySpecStatus CheckpointStore::load()
{
    for (auto& flag : _exists) { flag = 0; }
    _latest = 0;
    auto query = _storage.prepare(literalText("SELECT sequence, created_at, previous, step, state, description FROM checkpoint ORDER BY sequence"));
    if (!query) { return query.code; }
    while (true) {
        const auto stepped = query.value.step();
        if (!stepped) { return _storage.check(stepped.code); }
        if (!stepped.value) { break; }
        const Row row = query.value.row();
        const int64_t sequence = row.integer(0);
        if (sequence < 1 || static_cast<uint64_t>(sequence) > _records.size()) {
            return _storage.reject(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, {literalText("체크포인트 수가 max_checkpoints 를 넘음")});
        }
        CheckpointRecord& record = _records[static_cast<size_t>(sequence - 1)];
        record.created_at = row.integer(1);
        record.previous = CheckpointHandle{static_cast<uint32_t>(row.integer(2)), 0};
        record.step = static_cast<entity::Step>(row.integer(3));
        record.state = static_cast<CheckpointState>(entity::parseState(viewOf(row.text(4))));
        if (record.description.assign(row.text(5)) != KIOTTY_SPEC_STATUS_SUCCESS) { record.description.clear(); }
        _exists[static_cast<size_t>(sequence - 1)] = 1;
        _latest = static_cast<uint32_t>(sequence);
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<const CheckpointRecord*, KiottySpecStatus> CheckpointStore::get(uint32_t sequence) const
{
    const CheckpointRecord* record = at(sequence);
    if (record == nullptr) { return Result<const CheckpointRecord*, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND); }
    return Result<const CheckpointRecord*, KiottySpecStatus>::success(record);
}

const CheckpointRecord* CheckpointStore::at(uint32_t sequence) const
{
    if (sequence == 0 || sequence > _records.size() || _exists[sequence - 1] == 0) { return nullptr; }
    return &_records[sequence - 1];
}

Result<uint64_t, KiottySpecStatus> CheckpointStore::countWrites(uint32_t sequence)
{
    using CountResult = Result<uint64_t, KiottySpecStatus>;
    auto query = _storage.prepare(literalText(
        "SELECT (SELECT COUNT(*) FROM item_version WHERE checkpoint = ?1) + (SELECT COUNT(*) FROM relation_version WHERE checkpoint = ?1)"
        " + (SELECT COUNT(*) FROM decision_version WHERE checkpoint = ?1)"));
    if (!query) { return CountResult::failure(query.code); }
    query.value.bind(1, static_cast<int64_t>(sequence));
    const auto stepped = query.value.step();
    if (!stepped) { return CountResult::failure(_storage.check(stepped.code)); }
    return CountResult::success(static_cast<uint64_t>(query.value.row().integer(0)));
}

}
