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

Result<uint32_t, KiottySpecStatus> CheckpointStore::create(entity::Step step, Span<const char> description, int64_t created_at)
{
    using SequenceResult = Result<uint32_t, KiottySpecStatus>;
    const uint32_t sequence = _latest + 1;
    if (sequence > _records.size()) {
        return SequenceResult::failure(_storage.reject(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, {literalText("체크포인트 수가 max_checkpoints 에 닿음")}));
    }
    CheckpointRecord record {};
    record.created_at = created_at;
    record.previous = CheckpointHandle{_latest, 0};
    record.step = step;
    record.state = CheckpointState::STARTED;
    const KiottySpecStatus copied = record.description.assign(description);
    if (copied != KIOTTY_SPEC_STATUS_SUCCESS) {
        return SequenceResult::failure(_storage.reject(copied, {literalText("체크포인트 설명은 63 바이트 이하")}));
    }
    auto query = _storage.prepare(literalText(
        "INSERT INTO checkpoint (sequence, created_at, previous, step, state, description) VALUES (?1, ?2, ?3, ?4, ?5, ?6)"));
    if (!query) { return SequenceResult::failure(query.code); }
    query.value.bind(1, static_cast<int64_t>(sequence));
    query.value.bind(2, created_at);
    query.value.bind(3, static_cast<int64_t>(_latest));
    query.value.bind(4, static_cast<int64_t>(step));
    query.value.bind(5, spanOf(entity::stateName(CheckpointState::STARTED)));
    query.value.bind(6, description);
    const KiottySpecStatus inserted = _storage.check(query.value.execute());
    if (inserted != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(inserted); }
    _records[sequence - 1] = record;
    _exists[sequence - 1] = 1;
    _latest = sequence;
    return SequenceResult::success(sequence);
}

KiottySpecStatus CheckpointStore::setState(uint32_t sequence, CheckpointState state)
{
    if (at(sequence) == nullptr) { return KIOTTY_SPEC_STATUS_NOT_FOUND; }
    auto query = _storage.prepare(literalText("UPDATE checkpoint SET state = ?2 WHERE sequence = ?1"));
    if (!query) { return query.code; }
    query.value.bind(1, static_cast<int64_t>(sequence));
    query.value.bind(2, spanOf(entity::stateName(state)));
    const KiottySpecStatus updated = _storage.check(query.value.execute());
    if (updated != KIOTTY_SPEC_STATUS_SUCCESS) { return updated; }
    _records[sequence - 1].state = state;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<size_t, KiottySpecStatus> CheckpointStore::dropBefore(uint32_t base)
{
    using CountResult = Result<size_t, KiottySpecStatus>;
    auto removed = _storage.prepare(literalText("DELETE FROM checkpoint WHERE sequence < ?1"));
    if (!removed) { return CountResult::failure(removed.code); }
    removed.value.bind(1, static_cast<int64_t>(base));
    const KiottySpecStatus deleted = _storage.check(removed.value.execute());
    if (deleted != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(deleted); }
    const size_t count = static_cast<size_t>(removed.value.changedRowCount());
    auto detached = _storage.prepare(literalText("UPDATE checkpoint SET previous = 0 WHERE sequence = ?1"));
    if (!detached) { return CountResult::failure(detached.code); }
    detached.value.bind(1, static_cast<int64_t>(base));
    const KiottySpecStatus updated = _storage.check(detached.value.execute());
    if (updated != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(updated); }
    for (uint32_t sequence = 1; sequence < base && sequence <= _records.size(); ++sequence) { _exists[sequence - 1] = 0; }
    if (base >= 1 && base <= _records.size()) { _records[base - 1].previous = entity::INVALID_CHECKPOINT_HANDLE; }
    return CountResult::success(count);
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
