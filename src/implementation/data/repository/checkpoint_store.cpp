#include "implementation/data/repository/checkpoint_store.h"

#include "implementation/core/text.h"
#include "implementation/core/text_view.h"
#include "implementation/data/datasource/savepoint.h"
#include "implementation/data/datasource/schema.h"
#include "implementation/domain/entity/names.h"

#include <string>

namespace kiotty::spec::implementation {

namespace {

constexpr int32_t FIRST_STEP = 1;
constexpr int32_t LAST_STEP = 10;

constexpr char INSERT_SQL[] =
    "INSERT INTO checkpoint (step, confirmed, purpose, note, created_at) VALUES (?1, 0, ?2, ?3, ?4)";
constexpr char BY_SEQ_SQL[] = "SELECT seq, step, confirmed, purpose, note, created_at FROM checkpoint WHERE seq = ?1";
constexpr char LATEST_SQL[] =
    "SELECT seq, step, confirmed, purpose, note, created_at FROM checkpoint ORDER BY seq DESC LIMIT 1";
constexpr char LAST_CONFIRMED_SQL[] =
    "SELECT seq, step, confirmed, purpose, note, created_at FROM checkpoint"
    " WHERE confirmed = 1 AND (?1 = 0 OR step = ?1) ORDER BY seq DESC LIMIT 1";
constexpr char LIST_SQL[] =
    "SELECT seq, step, confirmed, purpose, note, created_at FROM checkpoint WHERE (?1 = 0 OR step = ?1) ORDER BY seq";
constexpr char CONFIRM_SQL[] = "UPDATE checkpoint SET confirmed = 1 WHERE seq = ?1";

constexpr std::string_view PROMOTE_SQL[] = {
    "UPDATE milestone_state SET implemented_cp_seq = ?1 WHERE implemented_cp_seq = ?2",
    "UPDATE result SET cp_seq = ?1 WHERE cp_seq = ?2",
    "UPDATE change_request SET cp_opened = ?1 WHERE cp_opened = ?2",
    "UPDATE change_request SET cp_closed = ?1 WHERE cp_closed = ?2",
};

constexpr char KEEP_MILESTONE_SQL[] = "UPDATE milestone_state SET implemented_cp_seq = ?1 WHERE implemented_cp_seq < ?1";
constexpr char DELETE_RESULTS_SQL[] = "DELETE FROM result WHERE cp_seq < ?1";
constexpr char DELETE_CLOSED_SQL[] = "DELETE FROM change_request WHERE state IN ('done', 'rejected') AND cp_closed < ?1";
constexpr char KEEP_OPENED_SQL[] = "UPDATE change_request SET cp_opened = ?1 WHERE cp_opened < ?1";
constexpr char DELETE_CHECKPOINTS_SQL[] = "DELETE FROM checkpoint WHERE seq < ?1";

CheckpointRecord readCheckpoint(const Row &row)
{
    CheckpointRecord record;
    record.seq = row.integer(0);
    record.step = static_cast<int32_t>(row.integer(1));
    record.confirmed = row.integer(2) != 0;
    parse(viewOf(row.text(3)), record.purpose);
    record.note = std::string(viewOf(row.text(4)));
    record.createdAt = row.integer(5);
    return record;
}

}

SequenceResult CheckpointStore::create(int32_t step, std::string_view note, KiottySpecCheckpointPurpose purpose)
{
    const bool inRange = step >= FIRST_STEP && step <= LAST_STEP;
    if (!inRange) { return SequenceResult::failure(_storage.reject(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("step 은 1 ~ 10 이어야 함")})); }
    Savepoint savepoint(_storage.connection());
    const KiottySpecStatus begun = _storage.check(savepoint.begin());
    if (begun != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(begun); }
    const auto seq = insert(step, note, purpose);
    if (!seq) { return seq; }
    const KiottySpecStatus promoted = promotePending(seq.value);
    if (promoted != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(promoted); }
    const KiottySpecStatus committed = _storage.check(savepoint.commit());
    if (committed != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(committed); }
    return seq;
}

OptionalCheckpointResult CheckpointStore::find(int64_t seq) { return first(BY_SEQ_SQL, seq); }

CheckpointResult CheckpointStore::get(int64_t seq)
{
    const auto found = find(seq);
    if (!found) { return CheckpointResult::failure(found.code); }
    if (!found.value) {
        const std::string name = checkpointName(seq);
        return CheckpointResult::failure(_storage.reject(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("체크포인트 없음: "), spanOf(name)}));
    }
    return CheckpointResult::success(*found.value);
}

OptionalCheckpointResult CheckpointStore::latest()
{
    auto query = _storage.prepare(literalText(LATEST_SQL));
    if (!query) { return OptionalCheckpointResult::failure(query.code); }
    return single(query.value);
}

OptionalCheckpointResult CheckpointStore::lastConfirmed(int32_t stepOr0) { return first(LAST_CONFIRMED_SQL, stepOr0); }

CheckpointListResult CheckpointStore::list(int32_t stepOr0)
{
    auto query = _storage.prepare(literalText(LIST_SQL));
    if (!query) { return CheckpointListResult::failure(query.code); }
    query.value.bind(1, static_cast<int64_t>(stepOr0));
    std::vector<CheckpointRecord> records;
    auto stepped = query.value.step();
    while (stepped.value) {
        records.push_back(readCheckpoint(query.value.row()));
        stepped = query.value.step();
    }
    if (!stepped) { return CheckpointListResult::failure(_storage.check(stepped.code)); }
    return CheckpointListResult::success(std::move(records));
}

KiottySpecStatus CheckpointStore::markConfirmed(int64_t seq) { return change(CONFIRM_SQL, seq).code; }

SequenceResult CheckpointStore::resolve(std::string_view cpId)
{
    const auto seq = parseCheckpointName(cpId);
    if (!seq) { return SequenceResult::failure(_storage.reject(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("체크포인트 ID 형식 오류: "), spanOf(cpId)})); }
    if (*seq == kPending) { return SequenceResult::success(*seq); }
    const auto record = get(*seq);
    if (!record) { return SequenceResult::failure(record.code); }
    return SequenceResult::success(*seq);
}

CountResult CheckpointStore::pendingWriteCount()
{
    size_t total = 0;
    for (const VersionedTable& table : versionedTables()) {
        auto query = _storage.prepare(spanOf(table.count_pending_sql));
        if (!query) { return CountResult::failure(query.code); }
        query.value.bind(1, kPending);
        const auto stepped = query.value.step();
        if (!stepped) { return CountResult::failure(_storage.check(stepped.code)); }
        total += static_cast<size_t>(query.value.row().integer(0));
    }
    return CountResult::success(total);
}

CountResult CheckpointStore::prune(int64_t baseSeq)
{
    Savepoint savepoint(_storage.connection());
    const KiottySpecStatus begun = _storage.check(savepoint.begin());
    if (begun != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(begun); }
    const auto tables = pruneTables(baseSeq);
    if (!tables) { return tables; }
    const auto history = pruneHistory(baseSeq);
    if (!history) { return history; }
    const KiottySpecStatus committed = _storage.check(savepoint.commit());
    if (committed != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(committed); }
    return CountResult::success(tables.value + history.value);
}

SequenceResult CheckpointStore::insert(int32_t step, std::string_view note, KiottySpecCheckpointPurpose purpose)
{
    auto query = _storage.prepare(literalText(INSERT_SQL));
    if (!query) { return SequenceResult::failure(query.code); }
    query.value.bind(1, static_cast<int64_t>(step));
    query.value.bind(2, spanOf(name(purpose)));
    query.value.bind(3, spanOf(note));
    query.value.bind(4, nowEpochSeconds());
    const KiottySpecStatus ran = _storage.check(query.value.run());
    if (ran != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(ran); }
    return SequenceResult::success(_storage.insertedRowId());
}

KiottySpecStatus CheckpointStore::promotePending(int64_t seq)
{
    for (const VersionedTable& table : versionedTables()) {
        const auto promoted = change(table.promote_sql, seq, kPending);
        if (!promoted) { return promoted.code; }
    }
    for (const std::string_view sql : PROMOTE_SQL) {
        const auto promoted = change(sql, seq, kPending);
        if (!promoted) { return promoted.code; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

CountResult CheckpointStore::pruneTables(int64_t baseSeq)
{
    size_t removed = 0;
    for (const VersionedTable& table : versionedTables()) {
        const auto kept = change(table.keep_latest_sql, baseSeq);
        if (!kept) { return kept; }
        const auto older = change(table.delete_older_sql, baseSeq);
        if (!older) { return older; }
        removed += older.value;
        if (table.delete_removed_sql.empty()) { continue; }
        const auto gone = change(table.delete_removed_sql, baseSeq);
        if (!gone) { return gone; }
        removed += gone.value;
    }
    return CountResult::success(removed);
}

CountResult CheckpointStore::pruneHistory(int64_t baseSeq)
{
    const auto milestones = change(KEEP_MILESTONE_SQL, baseSeq);
    if (!milestones) { return milestones; }
    const auto results = change(DELETE_RESULTS_SQL, baseSeq);
    if (!results) { return results; }
    const auto closed = change(DELETE_CLOSED_SQL, baseSeq);
    if (!closed) { return closed; }
    const auto opened = change(KEEP_OPENED_SQL, baseSeq);
    if (!opened) { return opened; }
    const auto checkpoints = change(DELETE_CHECKPOINTS_SQL, baseSeq);
    if (!checkpoints) { return checkpoints; }
    return CountResult::success(results.value + closed.value + checkpoints.value);
}

CountResult CheckpointStore::change(std::string_view sql, int64_t first)
{
    auto query = _storage.prepare(spanOf(sql));
    if (!query) { return CountResult::failure(query.code); }
    query.value.bind(1, first);
    const KiottySpecStatus ran = _storage.check(query.value.run());
    if (ran != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(ran); }
    return CountResult::success(static_cast<size_t>(_storage.changedRowCount()));
}

CountResult CheckpointStore::change(std::string_view sql, int64_t first, int64_t second)
{
    auto query = _storage.prepare(spanOf(sql));
    if (!query) { return CountResult::failure(query.code); }
    query.value.bind(1, first);
    query.value.bind(2, second);
    const KiottySpecStatus ran = _storage.check(query.value.run());
    if (ran != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(ran); }
    return CountResult::success(static_cast<size_t>(_storage.changedRowCount()));
}

OptionalCheckpointResult CheckpointStore::first(std::string_view sql, int64_t value)
{
    auto query = _storage.prepare(spanOf(sql));
    if (!query) { return OptionalCheckpointResult::failure(query.code); }
    query.value.bind(1, value);
    return single(query.value);
}

OptionalCheckpointResult CheckpointStore::single(Query &query)
{
    const auto stepped = query.step();
    if (!stepped) { return OptionalCheckpointResult::failure(_storage.check(stepped.code)); }
    if (!stepped.value) { return OptionalCheckpointResult::success(std::nullopt); }
    return OptionalCheckpointResult::success(readCheckpoint(query.row()));
}

}
