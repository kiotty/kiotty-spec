#include "implementation/base/result_store.h"

#include "implementation/core/error.h"
#include "implementation/core/text.h"

namespace kiotty::base {

using core::ResultRecord;

namespace {

ResultRecord readResult(const core::Statement &stmt) {
    ResultRecord record;
    record.targetId = stmt.text(0);
    core::parse(stmt.text(1), record.result);
    record.log = stmt.text(2);
    record.cpSeq = stmt.integer(3);
    record.runAt = stmt.integer(4);
    return record;
}

bool canHaveResult(KiottySpecItemKind kind) { return core::isTestKind(kind) || kind == KiottySpecItemKind_Rule; }

} // namespace

void ResultStore::record(std::string_view targetId, KiottySpecTestResult result, std::string_view log) {
    const auto target = items_.get(targetId);
    core::require(canHaveResult(target.kind), KiottySpecStatus_InvalidArg,
                  "결과는 테스트 또는 (코드 검사) 개발 규칙에만 기록할 수 있음: " + target.id);
    core::require(result == KiottySpecTestResult_Pass || result == KiottySpecTestResult_Fail,
                  KiottySpecStatus_InvalidArg, "결과는 pass 또는 fail");
    db_.prepare("INSERT INTO result (target_id, result, log, cp_seq, run_at) VALUES (?1, ?2, ?3, ?4, ?5)")
        .bindAll(targetId, core::name(result), log, core::kPending, core::nowEpochSeconds())
        .run();
}

std::optional<ResultRecord> ResultStore::last(std::string_view targetId) {
    auto stmt = db_.prepare("SELECT target_id, result, log, cp_seq, run_at FROM result WHERE target_id = ?1 ORDER BY id DESC LIMIT 1");
    stmt.bindAll(targetId);
    if (!stmt.step()) {
        return std::nullopt;
    }
    return readResult(stmt);
}

std::vector<ResultRecord> ResultStore::history(std::string_view targetId) {
    auto stmt = db_.prepare("SELECT target_id, result, log, cp_seq, run_at FROM result WHERE target_id = ?1 ORDER BY id");
    stmt.bindAll(targetId);
    std::vector<ResultRecord> records;
    while (stmt.step()) {
        records.push_back(readResult(stmt));
    }
    return records;
}

std::vector<core::ItemRecord> ResultStore::failed(KiottySpecItemKind kindOr0) {
    std::vector<core::ItemRecord> result;
    const std::vector<KiottySpecItemKind> kinds =
        kindOr0 != 0 ? std::vector<KiottySpecItemKind>{kindOr0}
                     : std::vector<KiottySpecItemKind>{KiottySpecItemKind_UnitTest, KiottySpecItemKind_SystemTest,
                                                       KiottySpecItemKind_E2eTest, KiottySpecItemKind_Rule};
    for (const auto kind : kinds) {
        for (auto &item : items_.ofKind(kind)) {
            const auto latest = last(item.id);
            if (latest && latest->result == KiottySpecTestResult_Fail) {
                result.push_back(std::move(item));
            }
        }
    }
    return result;
}

ResultSummary ResultStore::summary(KiottySpecItemKind kind) {
    ResultSummary summary;
    for (const auto &item : items_.ofKind(kind)) {
        ++summary.total;
        const auto latest = last(item.id);
        if (!latest) {
            ++summary.notRun;
        } else if (latest->result == KiottySpecTestResult_Pass) {
            ++summary.pass;
        } else {
            ++summary.fail;
        }
    }
    return summary;
}

} // namespace kiotty::base
