#include "implementation/data/repository/result_store.h"

#include "implementation/core/text.h"
#include "implementation/core/text_view.h"
#include "implementation/data/repository/record_rows.h"

namespace kiotty::spec::implementation {

namespace {

constexpr char INSERT_SQL[] = "INSERT INTO result (target_id, result, log, cp_seq, run_at) VALUES (?1, ?2, ?3, ?4, ?5)";
constexpr char LAST_SQL[] =
    "SELECT target_id, result, log, cp_seq, run_at FROM result WHERE target_id = ?1 ORDER BY id DESC LIMIT 1";
constexpr char HISTORY_SQL[] = "SELECT target_id, result, log, cp_seq, run_at FROM result WHERE target_id = ?1 ORDER BY id";

constexpr KiottySpecItemKind CHECKED_KINDS[] = {
    KIOTTY_SPEC_ITEM_KIND_UNIT_TEST,
    KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST,
    KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST,
    KIOTTY_SPEC_ITEM_KIND_RULE,
};

bool canHaveResult(KiottySpecItemKind kind) { return isTestKind(kind) || kind == KIOTTY_SPEC_ITEM_KIND_RULE; }

void count(ResultSummary &summary, const std::optional<ResultRecord> &latest)
{
    ++summary.total;
    if (!latest) {
        ++summary.notRun;
        return;
    }
    if (latest->result == KIOTTY_SPEC_TEST_RESULT_PASS) {
        ++summary.pass;
        return;
    }
    ++summary.fail;
}

}

KiottySpecStatus ResultStore::record(std::string_view targetId, KiottySpecTestResult result, std::string_view log)
{
    const auto target = _items.get(targetId);
    if (!target) { return target.code; }
    if (!canHaveResult(target.value.kind)) { return _storage.reject(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("결과는 테스트 또는 (코드 검사) 개발 규칙에만 기록할 수 있음: "), target.value.id.view()}); }
    const bool isVerdict = result == KIOTTY_SPEC_TEST_RESULT_PASS || result == KIOTTY_SPEC_TEST_RESULT_FAIL;
    if (!isVerdict) { return _storage.reject(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("결과는 pass 또는 fail")}); }
    return insert(targetId, result, log);
}

OptionalTestRunResult ResultStore::last(std::string_view targetId)
{
    auto query = _storage.prepare(literalText(LAST_SQL));
    if (!query) { return OptionalTestRunResult::failure(query.code); }
    query.value.bind(1, spanOf(targetId));
    const auto stepped = query.value.step();
    if (!stepped) { return OptionalTestRunResult::failure(_storage.check(stepped.code)); }
    if (!stepped.value) { return OptionalTestRunResult::success(std::nullopt); }
    auto record = readTestRun(_storage, query.value.row());
    if (!record) { return OptionalTestRunResult::failure(record.code); }
    return OptionalTestRunResult::success(std::move(record.value));
}

TestRunListResult ResultStore::history(std::string_view targetId)
{
    auto query = _storage.prepare(literalText(HISTORY_SQL));
    if (!query) { return TestRunListResult::failure(query.code); }
    query.value.bind(1, spanOf(targetId));
    return collectTestRuns(_storage, query.value);
}

ItemListResult ResultStore::failed(KiottySpecItemKind kindOr0)
{
    if (kindOr0 != 0) { return failedOfKind(kindOr0); }
    std::vector<ItemRecord> result;
    for (const KiottySpecItemKind kind : CHECKED_KINDS) {
        auto items = failedOfKind(kind);
        if (!items) { return items; }
        for (ItemRecord &item : items.value) { result.push_back(std::move(item)); }
    }
    return ItemListResult::success(std::move(result));
}

ResultSummaryResult ResultStore::summary(KiottySpecItemKind kind)
{
    const auto items = _finder.ofKind(kind);
    if (!items) { return ResultSummaryResult::failure(items.code); }
    ResultSummary summary;
    for (const ItemRecord &item : items.value) {
        const auto latest = last(item.id);
        if (!latest) { return ResultSummaryResult::failure(latest.code); }
        count(summary, latest.value);
    }
    return ResultSummaryResult::success(summary);
}

KiottySpecStatus ResultStore::insert(std::string_view targetId, KiottySpecTestResult result, std::string_view log)
{
    auto query = _storage.prepare(literalText(INSERT_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, spanOf(targetId));
    query.value.bind(2, spanOf(name(result)));
    query.value.bind(3, spanOf(log));
    query.value.bind(4, kPending);
    query.value.bind(5, nowEpochSeconds());
    return _storage.check(query.value.run());
}

ItemListResult ResultStore::failedOfKind(KiottySpecItemKind kind)
{
    auto items = _finder.ofKind(kind);
    if (!items) { return items; }
    std::vector<ItemRecord> result;
    for (ItemRecord &item : items.value) {
        const auto latest = last(item.id);
        if (!latest) { return ItemListResult::failure(latest.code); }
        const bool hasFailed = latest.value && latest.value->result == KIOTTY_SPEC_TEST_RESULT_FAIL;
        if (hasFailed) { result.push_back(std::move(item)); }
    }
    return ItemListResult::success(std::move(result));
}

}
