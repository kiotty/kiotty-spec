#include "implementation/context/kiotty_item_diff_query.h"

#include "implementation/core/kiotty_text_view.h"

#include <iterator>
#include <utility>

namespace kiotty::spec::implementation {

namespace {

constexpr uint32_t MAX_KIND_BIT = 32;

constexpr char DIFF_SQL[] =
    "WITH f AS (SELECT v.item_key, v.checkpoint, v.kind, v.parent_key, v.title, v.summary, v.body, v.tags, v.removed "
    "FROM item_version v JOIN (SELECT item_key, MAX(checkpoint) AS c FROM item_version WHERE checkpoint <= ?1 GROUP "
    "BY item_key) m ON v.item_key = m.item_key AND v.checkpoint = m.c), t AS (SELECT v.item_key, v.checkpoint, v.kind, "
    "v.parent_key, v.title, v.summary, v.body, v.tags, v.removed FROM item_version v JOIN (SELECT item_key, "
    "MAX(checkpoint) AS c FROM item_version WHERE checkpoint <= ?2 GROUP BY item_key) m ON v.item_key = m.item_key AND "
    "v.checkpoint = m.c), k AS (SELECT item_key FROM f UNION SELECT item_key FROM t) SELECT i.id, f.checkpoint, "
    "f.removed, t.checkpoint, t.removed, COALESCE(t.kind, f.kind), f.title IS NOT t.title, f.summary IS NOT "
    "t.summary, f.parent_key IS NOT t.parent_key, f.tags IS NOT t.tags, f.body IS NOT t.body FROM k JOIN item i ON "
    "i.key = k.item_key LEFT JOIN f ON f.item_key = k.item_key LEFT JOIN t ON t.item_key = k.item_key "
    "ORDER BY CASE COALESCE(t.kind, f.kind) WHEN "
    "'change_request' THEN 1 WHEN 'requirement' THEN 2 WHEN 'rule' THEN 3 WHEN 'external_interface' THEN 4 WHEN "
    "'design' THEN 5 WHEN 'unit_test_plan' THEN 6 WHEN 'milestone' THEN 7 WHEN 'unit_test_result' THEN 8 WHEN "
    "'system_test_plan' THEN 9 WHEN 'system_test_result' THEN 10 WHEN 'code_inspection_result' THEN 11 WHEN "
    "'e2e_test_plan' THEN 12 WHEN 'e2e_test_result' THEN 13 ELSE 14 END, i.id";

bool inMask(uint32_t mask, uint32_t kind) { return mask == 0 || (kind >= 1 && kind <= MAX_KIND_BIT && (mask & (1u << (kind - 1))) != 0); }

}

KiottySpecStatus ItemDiffQuery::open()
{
    auto prepared = _storage.prepare(literalText(DIFF_SQL));
    if (!prepared) { return prepared.code; }
    _statement = std::move(prepared.value);
    _statement.bind(1, static_cast<int64_t>(_range.from));
    _statement.bind(2, static_cast<int64_t>(_range.to));
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<bool, KiottySpecStatus> ItemDiffQuery::next()
{
    Result<bool, KiottySpecStatus> stepped = _statement.step();
    while (stepped && stepped.value && !decode(_statement.row())) { stepped = _statement.step(); }
    if (!stepped) { return Result<bool, KiottySpecStatus>::failure(_storage.check(stepped.code)); }
    return stepped;
}

// 앞 시점에만 있으면 추가가 아니라 삭제, 두 시점의 버전이 다르면 바뀐 필드를 모은다.
bool ItemDiffQuery::decode(const Row& row)
{
    const uint32_t flags[] = {KIOTTY_SPEC_CHANGED_FIELD_TITLE, KIOTTY_SPEC_CHANGED_FIELD_SUMMARY, KIOTTY_SPEC_CHANGED_FIELD_PARENT,
                              KIOTTY_SPEC_CHANGED_FIELD_TAGS, KIOTTY_SPEC_CHANGED_FIELD_BODY};
    const int first_flag_column = 6;
    const uint32_t kind = entity::parseKind(viewOf(row.text(5)));
    const bool before = !row.isNull(1) && row.integer(2) == 0;
    const bool after = !row.isNull(3) && row.integer(4) == 0;
    const bool modified = before && after && row.integer(1) != row.integer(3);
    _current = ItemDiffRow{};
    _current.id.assign(row.text(0));
    _current.kind = kind;
    _current.before = before ? static_cast<uint32_t>(row.integer(1)) : 0;
    _current.after = row.isNull(3) ? 0 : static_cast<uint32_t>(row.integer(3));
    for (int index = 0; modified && index < static_cast<int>(std::size(flags)); ++index) {
        if (row.integer(first_flag_column + index) != 0) { _current.fields |= flags[index]; }
    }
    const bool wanted = _current.fields != 0 && (_field_mask == 0 || (_current.fields & _field_mask) != 0);
    if (!before && after) { _current.change = KIOTTY_SPEC_CHANGE_ADDED; }
    if (before && !after) { _current.change = KIOTTY_SPEC_CHANGE_REMOVED; }
    if (modified && wanted) { _current.change = KIOTTY_SPEC_CHANGE_MODIFIED; }
    return inMask(_kind_mask, kind) && _current.change != 0;
}

}
