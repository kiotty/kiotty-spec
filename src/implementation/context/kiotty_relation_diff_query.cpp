#include "implementation/context/kiotty_relation_diff_query.h"

#include "implementation/core/kiotty_text_view.h"

#include <utility>

namespace kiotty::spec::implementation {

namespace {

constexpr uint32_t MAX_KIND_BIT = 32;

constexpr char DIFF_SQL[] =
    "WITH f AS (SELECT v.from_id, v.model, v.relation, v.to_id, v.checkpoint, v.removed FROM relation_version v "
    "JOIN (SELECT from_id, relation, to_id, MAX(checkpoint) AS c FROM relation_version WHERE checkpoint <= ?1 GROUP "
    "BY from_id, relation, to_id) m ON v.from_id = m.from_id AND v.relation = m.relation AND v.to_id = m.to_id AND "
    "v.checkpoint = m.c), t AS (SELECT v.from_id, v.model, v.relation, v.to_id, v.checkpoint, v.removed FROM "
    "relation_version v JOIN (SELECT from_id, relation, to_id, MAX(checkpoint) AS c FROM relation_version WHERE "
    "checkpoint <= ?2 GROUP BY from_id, relation, to_id) m ON v.from_id = m.from_id AND v.relation = m.relation AND "
    "v.to_id = m.to_id AND v.checkpoint = m.c), k AS (SELECT from_id, relation, to_id FROM f UNION SELECT from_id, "
    "relation, to_id FROM t) SELECT k.from_id, k.relation, k.to_id, COALESCE(t.model, f.model), f.removed, "
    "t.removed, t.checkpoint FROM k LEFT JOIN f ON f.from_id = k.from_id AND f.relation = k.relation AND f.to_id = "
    "k.to_id LEFT JOIN t ON t.from_id = k.from_id AND t.relation = k.relation AND t.to_id = k.to_id ORDER BY CASE "
    "COALESCE(t.model, f.model) WHEN 'change_request' THEN 1 WHEN 'requirement' THEN 2 WHEN 'rule' THEN 3 WHEN "
    "'external_interface' THEN 4 WHEN 'design' THEN 5 WHEN 'unit_test_plan' THEN 6 WHEN 'milestone' THEN 7 WHEN "
    "'unit_test_result' THEN 8 WHEN 'system_test_plan' THEN 9 WHEN 'system_test_result' THEN 10 WHEN "
    "'code_inspection_result' THEN 11 WHEN 'e2e_test_plan' THEN 12 WHEN 'e2e_test_result' THEN 13 ELSE 14 END, "
    "k.from_id, k.relation, k.to_id";

bool inMask(uint32_t mask, uint32_t kind) { return mask == 0 || (kind >= 1 && kind <= MAX_KIND_BIT && (mask & (1u << (kind - 1))) != 0); }

}

KiottySpecStatus RelationDiffQuery::open()
{
    auto prepared = _storage.prepare(literalText(DIFF_SQL));
    if (!prepared) { return prepared.code; }
    _statement = std::move(prepared.value);
    _statement.bind(1, static_cast<int64_t>(_range.from));
    _statement.bind(2, static_cast<int64_t>(_range.to));
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<bool, KiottySpecStatus> RelationDiffQuery::next()
{
    Result<bool, KiottySpecStatus> stepped = _statement.step();
    while (stepped && stepped.value && !decode(_statement.row())) { stepped = _statement.step(); }
    if (!stepped) { return Result<bool, KiottySpecStatus>::failure(_storage.check(stepped.code)); }
    return stepped;
}

// 한 시점에만 살아 있는 관계가 diff 다.
bool RelationDiffQuery::decode(const Row& row)
{
    const uint32_t model = entity::parseKind(viewOf(row.text(3)));
    const bool before = !row.isNull(4) && row.integer(4) == 0;
    const bool after = !row.isNull(5) && row.integer(5) == 0;
    if (!inMask(_kind_mask, model) || before == after) { return false; }
    _current = RelationDiffRow{};
    _current.from_id.assign(row.text(0));
    _current.to_id.assign(row.text(2));
    _current.relation = static_cast<uint32_t>(row.integer(1));
    _current.model = model;
    _current.checkpoint = static_cast<uint32_t>(row.integer(6));
    _current.change = after ? KIOTTY_SPEC_CHANGE_ADDED : KIOTTY_SPEC_CHANGE_REMOVED;
    return true;
}

}
