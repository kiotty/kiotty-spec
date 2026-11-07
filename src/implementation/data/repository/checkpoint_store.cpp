#include "implementation/data/repository/checkpoint_store.h"

#include "implementation/data/datasource/schema.h"
#include "implementation/core/error.h"
#include "implementation/domain/entity/names.h"
#include "implementation/core/text.h"

#include <string>

namespace kiotty::spec::implementation {

namespace {

constexpr std::string_view kSelect = "SELECT seq, step, confirmed, purpose, note, created_at FROM checkpoint ";

CheckpointRecord readCheckpoint(const Statement &stmt) {
    CheckpointRecord record;
    record.seq = stmt.integer(0);
    record.step = static_cast<int32_t>(stmt.integer(1));
    record.confirmed = stmt.integer(2) != 0;
    parse(stmt.text(3), record.purpose);
    record.note = stmt.text(4);
    record.createdAt = stmt.integer(5);
    return record;
}

}

int64_t CheckpointStore::create(int32_t step, std::string_view note, KiottySpecCheckpointPurpose purpose) {
    require(step >= 1 && step <= 10, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "step 은 1 ~ 10 이어야 함");
    LegacySavepoint savepoint(db_);
    db_.prepare("INSERT INTO checkpoint (step, confirmed, purpose, note, created_at) VALUES (?1, 0, ?2, ?3, ?4)")
        .bindAll(step, name(purpose), note, nowEpochSeconds())
        .run();
    const int64_t seq = db_.lastInsertRowId();

    for (const auto &table : versionedTables()) {
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

std::optional<CheckpointRecord> CheckpointStore::find(int64_t seq) {
    auto stmt = db_.prepare(std::string(kSelect) + "WHERE seq = ?1");
    stmt.bindAll(seq);
    if (!stmt.step()) {
        return std::nullopt;
    }
    return readCheckpoint(stmt);
}

CheckpointRecord CheckpointStore::get(int64_t seq) {
    auto record = find(seq);
    require(record.has_value(), KIOTTY_SPEC_STATUS_NOT_FOUND, "체크포인트 없음: " + checkpointName(seq));
    return *record;
}

std::optional<CheckpointRecord> CheckpointStore::latest() {
    auto stmt = db_.prepare(std::string(kSelect) + "ORDER BY seq DESC LIMIT 1");
    if (!stmt.step()) {
        return std::nullopt;
    }
    return readCheckpoint(stmt);
}

std::optional<CheckpointRecord> CheckpointStore::lastConfirmed(int32_t stepOr0) {
    auto stmt = db_.prepare(std::string(kSelect) +
                            "WHERE confirmed = 1 AND (?1 = 0 OR step = ?1) ORDER BY seq DESC LIMIT 1");
    stmt.bindAll(stepOr0);
    if (!stmt.step()) {
        return std::nullopt;
    }
    return readCheckpoint(stmt);
}

std::vector<CheckpointRecord> CheckpointStore::list(int32_t stepOr0) {
    auto stmt = db_.prepare(std::string(kSelect) + "WHERE (?1 = 0 OR step = ?1) ORDER BY seq");
    stmt.bindAll(stepOr0);
    std::vector<CheckpointRecord> records;
    while (stmt.step()) {
        records.push_back(readCheckpoint(stmt));
    }
    return records;
}

void CheckpointStore::markConfirmed(int64_t seq) {
    db_.prepare("UPDATE checkpoint SET confirmed = 1 WHERE seq = ?1").bindAll(seq).run();
}

int64_t CheckpointStore::resolve(std::string_view cpId) {
    const auto seq = parseCheckpointName(cpId);
    require(seq.has_value(), KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "체크포인트 ID 형식 오류: " + std::string(cpId));
    if (*seq != kPending) {
        get(*seq);
    }
    return *seq;
}

size_t CheckpointStore::pendingWriteCount() {
    size_t total = 0;
    for (const auto &table : versionedTables()) {
        auto stmt = db_.prepare("SELECT COUNT(*) FROM " + std::string(table.name) + " WHERE cp_seq = ?1");
        stmt.bindAll(kPending);
        stmt.step();
        total += static_cast<size_t>(stmt.integer(0));
    }
    return total;
}

size_t CheckpointStore::prune(int64_t baseSeq) {
    LegacySavepoint savepoint(db_);
    size_t removed = 0;
    for (const auto &table : versionedTables()) {
        const std::string name(table.name);
        const std::string keys(table.keyColumns);
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

}
