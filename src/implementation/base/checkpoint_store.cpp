#include "implementation/base/checkpoint_store.h"

#include "implementation/base/schema.h"
#include "implementation/core/error.h"
#include "implementation/core/names.h"
#include "implementation/core/text.h"

#include <string>

namespace kiotty::base {

using core::kPending;

namespace {

constexpr std::string_view kSelect = "SELECT seq, step, confirmed, purpose, note, created_at FROM checkpoint ";

core::CheckpointRecord readCheckpoint(const core::Statement &stmt) {
    core::CheckpointRecord record;
    record.seq = stmt.integer(0);
    record.step = static_cast<int32_t>(stmt.integer(1));
    record.confirmed = stmt.integer(2) != 0;
    core::parse(stmt.text(3), record.purpose);
    record.note = stmt.text(4);
    record.createdAt = stmt.integer(5);
    return record;
}

} // namespace

int64_t CheckpointStore::create(int32_t step, std::string_view note, KiottySpecCheckpointPurpose purpose) {
    core::require(step >= 1 && step <= 10, KiottySpecStatus_InvalidArg, "step 은 1 ~ 10 이어야 함");
    core::Savepoint savepoint(db_);
    db_.prepare("INSERT INTO checkpoint (step, confirmed, purpose, note, created_at) VALUES (?1, 0, ?2, ?3, ?4)")
        .bindAll(step, core::name(purpose), note, core::nowEpochSeconds())
        .run();
    const int64_t seq = db_.lastInsertRowId();

    // 미기록 쓰기들을 이번 체크포인트로 묶는다.
    for (const auto &table : schema::versionedTables()) {
        db_.prepare("UPDATE " + std::string(table.name) + " SET cp_seq = ?1 WHERE cp_seq = ?2")
            .bindAll(seq, kPending)
            .run();
    }
    db_.prepare("UPDATE milestone_state SET implemented_cp_seq = ?1 WHERE implemented_cp_seq = ?2").bindAll(seq, kPending).run();
    db_.prepare("UPDATE result SET cp_seq = ?1 WHERE cp_seq = ?2").bindAll(seq, kPending).run();
    db_.prepare("UPDATE change_request SET cp_opened = ?1 WHERE cp_opened = ?2").bindAll(seq, kPending).run();
    db_.prepare("UPDATE change_request SET cp_closed = ?1 WHERE cp_closed = ?2").bindAll(seq, kPending).run();
    savepoint.commit();
    return seq;
}

std::optional<core::CheckpointRecord> CheckpointStore::find(int64_t seq) {
    auto stmt = db_.prepare(std::string(kSelect) + "WHERE seq = ?1");
    stmt.bindAll(seq);
    if (!stmt.step()) {
        return std::nullopt;
    }
    return readCheckpoint(stmt);
}

core::CheckpointRecord CheckpointStore::get(int64_t seq) {
    auto record = find(seq);
    core::require(record.has_value(), KiottySpecStatus_NotFound, "체크포인트 없음: " + core::checkpointName(seq));
    return *record;
}

std::optional<core::CheckpointRecord> CheckpointStore::latest() {
    auto stmt = db_.prepare(std::string(kSelect) + "ORDER BY seq DESC LIMIT 1");
    if (!stmt.step()) {
        return std::nullopt;
    }
    return readCheckpoint(stmt);
}

std::optional<core::CheckpointRecord> CheckpointStore::lastConfirmed(int32_t stepOr0) {
    auto stmt = db_.prepare(std::string(kSelect) +
                            "WHERE confirmed = 1 AND (?1 = 0 OR step = ?1) ORDER BY seq DESC LIMIT 1");
    stmt.bindAll(stepOr0);
    if (!stmt.step()) {
        return std::nullopt;
    }
    return readCheckpoint(stmt);
}

std::vector<core::CheckpointRecord> CheckpointStore::list(int32_t stepOr0) {
    auto stmt = db_.prepare(std::string(kSelect) + "WHERE (?1 = 0 OR step = ?1) ORDER BY seq");
    stmt.bindAll(stepOr0);
    std::vector<core::CheckpointRecord> records;
    while (stmt.step()) {
        records.push_back(readCheckpoint(stmt));
    }
    return records;
}

void CheckpointStore::markConfirmed(int64_t seq) {
    db_.prepare("UPDATE checkpoint SET confirmed = 1 WHERE seq = ?1").bindAll(seq).run();
}

int64_t CheckpointStore::resolve(std::string_view cpId) {
    const auto seq = core::parseCheckpointName(cpId);
    core::require(seq.has_value(), KiottySpecStatus_InvalidArg, "체크포인트 ID 형식 오류: " + std::string(cpId));
    if (*seq != kPending) {
        get(*seq);
    }
    return *seq;
}

size_t CheckpointStore::pendingWriteCount() {
    size_t total = 0;
    for (const auto &table : schema::versionedTables()) {
        auto stmt = db_.prepare("SELECT COUNT(*) FROM " + std::string(table.name) + " WHERE cp_seq = ?1");
        stmt.bindAll(kPending);
        stmt.step();
        total += static_cast<size_t>(stmt.integer(0));
    }
    return total;
}

size_t CheckpointStore::prune(int64_t baseSeq) {
    core::Savepoint savepoint(db_);
    size_t removed = 0;
    for (const auto &table : schema::versionedTables()) {
        const std::string name(table.name);
        const std::string keys(table.keyColumns);
        // 기준 시점에 유효했던 버전은 기준 체크포인트 소속으로 옮겨 살린다.
        db_.prepare("UPDATE " + name + " SET cp_seq = ?1 WHERE cp_seq < ?1 AND (" + keys + ", cp_seq) IN (SELECT " +
                    keys + ", MAX(cp_seq) FROM " + name + " WHERE cp_seq <= ?1 GROUP BY " + keys + ")")
            .bindAll(baseSeq)
            .run();
        db_.prepare("DELETE FROM " + name + " WHERE cp_seq < ?1").bindAll(baseSeq).run();
        removed += static_cast<size_t>(db_.changes());
        if (table.hasRemoved) {
            db_.prepare("DELETE FROM " + name + " WHERE removed = 1 AND cp_seq <= ?1").bindAll(baseSeq).run();
            removed += static_cast<size_t>(db_.changes());
        }
    }
    db_.prepare("UPDATE milestone_state SET implemented_cp_seq = ?1 WHERE implemented_cp_seq < ?1").bindAll(baseSeq).run();
    db_.prepare("DELETE FROM result WHERE cp_seq < ?1").bindAll(baseSeq).run();
    removed += static_cast<size_t>(db_.changes());
    db_.prepare("DELETE FROM change_request WHERE state IN ('done', 'rejected') AND cp_closed < ?1").bindAll(baseSeq).run();
    removed += static_cast<size_t>(db_.changes());
    db_.prepare("UPDATE change_request SET cp_opened = ?1 WHERE cp_opened < ?1").bindAll(baseSeq).run();
    db_.prepare("DELETE FROM checkpoint WHERE seq < ?1").bindAll(baseSeq).run();
    removed += static_cast<size_t>(db_.changes());
    savepoint.commit();
    return removed;
}

} // namespace kiotty::base
