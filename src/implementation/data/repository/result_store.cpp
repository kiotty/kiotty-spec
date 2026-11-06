#include "implementation/data/repository/result_store.h"

#include "implementation/core/error.h"
#include "implementation/core/text.h"

namespace kiotty::spec::implementation {

namespace {

ResultRecord readResult(const Statement &stmt) {
    ResultRecord record;
    record.targetId = stmt.text(0);
    parse(stmt.text(1), record.result);
    record.log = stmt.text(2);
    record.cpSeq = stmt.integer(3);
    record.runAt = stmt.integer(4);
    return record;
}

bool canHaveResult(KiottySpecItemKind kind) { return isTestKind(kind) || kind == KIOTTY_SPEC_ITEM_KIND_RULE; }

}

void ResultStore::record(std::string_view targetId, KiottySpecTestResult result, std::string_view log) {
    const auto target = items_.get(targetId);
    require(canHaveResult(target.kind), KIOTTY_SPEC_STATUS_INVALID_ARGUMENT,
                  "결과는 테스트 또는 (코드 검사) 개발 규칙에만 기록할 수 있음: " + target.id);
    require(result == KIOTTY_SPEC_TEST_RESULT_PASS || result == KIOTTY_SPEC_TEST_RESULT_FAIL,
                  KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "결과는 pass 또는 fail");
    db_.prepare("INSERT INTO result (target_id, result, log, cp_seq, run_at) VALUES (?1, ?2, ?3, ?4, ?5)")
        .bindAll(targetId, name(result), log, kPending, nowEpochSeconds())
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

std::vector<ItemRecord> ResultStore::failed(KiottySpecItemKind kindOr0) {
    std::vector<ItemRecord> result;
    const std::vector<KiottySpecItemKind> kinds =
        kindOr0 != 0 ? std::vector<KiottySpecItemKind>{kindOr0}
                     : std::vector<KiottySpecItemKind>{KIOTTY_SPEC_ITEM_KIND_UNIT_TEST, KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST,
                                                       KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST, KIOTTY_SPEC_ITEM_KIND_RULE};
    for (const auto kind : kinds) {
        for (auto &item : items_.ofKind(kind)) {
            const auto latest = last(item.id);
            if (latest && latest->result == KIOTTY_SPEC_TEST_RESULT_FAIL) {
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
        } else if (latest->result == KIOTTY_SPEC_TEST_RESULT_PASS) {
            ++summary.pass;
        } else {
            ++summary.fail;
        }
    }
    return summary;
}

}
