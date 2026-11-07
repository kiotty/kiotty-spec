#include "implementation/data/repository/version_history.h"

#include "implementation/core/text_view.h"
#include "implementation/data/datasource/savepoint.h"
#include "implementation/data/datasource/schema.h"
#include "implementation/domain/entity/names.h"

namespace kiotty::spec::implementation {

namespace {

constexpr char KEEP_MILESTONE_SQL[] = "UPDATE milestone_state SET implemented_cp_seq = ?1 WHERE implemented_cp_seq < ?1";
constexpr char DELETE_RESULTS_SQL[] = "DELETE FROM result WHERE cp_seq < ?1";
constexpr char DELETE_CLOSED_SQL[] = "DELETE FROM change_request WHERE state IN ('done', 'rejected') AND cp_closed < ?1";
constexpr char KEEP_OPENED_SQL[] = "UPDATE change_request SET cp_opened = ?1 WHERE cp_opened < ?1";
constexpr char DELETE_CHECKPOINTS_SQL[] = "DELETE FROM checkpoint WHERE seq < ?1";

}

CountResult VersionHistory::pendingCount()
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

CountResult VersionHistory::prune(int64_t baseSeq)
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

CountResult VersionHistory::pruneTables(int64_t baseSeq)
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

CountResult VersionHistory::pruneHistory(int64_t baseSeq)
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

CountResult VersionHistory::change(std::string_view sql, int64_t first)
{
    auto query = _storage.prepare(spanOf(sql));
    if (!query) { return CountResult::failure(query.code); }
    query.value.bind(1, first);
    const KiottySpecStatus ran = _storage.check(query.value.run());
    if (ran != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(ran); }
    return CountResult::success(static_cast<size_t>(_storage.changedRowCount()));
}

}
