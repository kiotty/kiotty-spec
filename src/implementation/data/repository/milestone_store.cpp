#include "implementation/data/repository/milestone_store.h"

#include "implementation/core/text_view.h"
#include "implementation/domain/entity/names.h"

namespace kiotty::spec::implementation {

namespace {

constexpr char WRITE_SQL[] =
    "INSERT INTO milestone_state (milestone_id, item_id, cp_seq, state, implemented_wseq, implemented_cp_seq)"
    " VALUES (?1, ?2, ?3, ?4, ?5, ?6)"
    " ON CONFLICT(milestone_id, item_id, cp_seq) DO UPDATE SET state = excluded.state,"
    " implemented_wseq = excluded.implemented_wseq, implemented_cp_seq = excluded.implemented_cp_seq";
constexpr char LATEST_SQL[] =
    "SELECT state, implemented_wseq, implemented_cp_seq FROM milestone_state"
    " WHERE milestone_id = ?1 AND item_id = ?2 AND cp_seq <= ?3 ORDER BY cp_seq DESC LIMIT 1";

void bindOptional(Query &query, int index, std::optional<int64_t> value)
{
    if (value) {
        query.bind(index, *value);
        return;
    }
    query.bind(index, nullptr);
}

std::optional<int64_t> optionalInteger(const Row &row, int column)
{
    if (row.isNull(column)) { return std::nullopt; }
    return row.integer(column);
}

}

KiottySpecStatus MilestoneStore::write(std::string_view milestoneId, std::string_view itemId, KiottySpecImplementationState state,
                                       std::optional<int64_t> implementedWseq)
{
    auto query = _storage.prepare(literalText(WRITE_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, spanOf(milestoneId));
    query.value.bind(2, spanOf(itemId));
    query.value.bind(3, kPending);
    query.value.bind(4, spanOf(name(state)));
    bindOptional(query.value, 5, implementedWseq);
    bindOptional(query.value, 6, implementedWseq ? std::optional<int64_t>(kPending) : std::nullopt);
    return _storage.check(query.value.run());
}

OptionalMilestoneStateResult MilestoneStore::latest(std::string_view milestoneId, std::string_view itemId, int64_t cut)
{
    auto query = _storage.prepare(literalText(LATEST_SQL));
    if (!query) { return OptionalMilestoneStateResult::failure(query.code); }
    query.value.bind(1, spanOf(milestoneId));
    query.value.bind(2, spanOf(itemId));
    query.value.bind(3, cut);
    const auto stepped = query.value.step();
    if (!stepped) { return OptionalMilestoneStateResult::failure(_storage.check(stepped.code)); }
    if (!stepped.value) { return OptionalMilestoneStateResult::success(std::nullopt); }
    const Row row = query.value.row();
    MilestoneState state;
    parse(viewOf(row.text(0)), state.state);
    state.implementedWseq = optionalInteger(row, 1);
    state.implementedCpSeq = optionalInteger(row, 2);
    return OptionalMilestoneStateResult::success(state);
}

}
