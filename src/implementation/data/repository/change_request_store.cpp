#include "implementation/data/repository/change_request_store.h"

#include "implementation/core/text.h"
#include "implementation/core/text_view.h"
#include "implementation/domain/entity/names.h"

#include <string>

namespace kiotty::spec::implementation {

namespace {

constexpr int ID_WIDTH = 3;
constexpr char ID_PREFIX[] = "CR";
constexpr int32_t FIRST_STEP = 1;
constexpr int32_t LAST_STEP = 10;

constexpr char INSERT_SQL[] =
    "INSERT INTO change_request (id, source, body, entry_step, state, cp_opened) VALUES (?1, ?2, ?3, ?4, 'open', ?5)";
constexpr char BY_ID_SQL[] =
    "SELECT id, source, body, entry_step, state, cp_opened, cp_closed, note FROM change_request WHERE id = ?1";
constexpr char LIST_SQL[] =
    "SELECT id, source, body, entry_step, state, cp_opened, cp_closed, note FROM change_request"
    " WHERE (?1 = '' OR state = ?1) ORDER BY id";
constexpr char SET_STATE_SQL[] =
    "UPDATE change_request SET state = ?2, cp_closed = ?3, note = CASE WHEN ?4 = '' THEN note ELSE ?4 END WHERE id = ?1";
constexpr char SET_ENTRY_STEP_SQL[] = "UPDATE change_request SET entry_step = ?2 WHERE id = ?1";
constexpr char OPEN_COUNT_SQL[] = "SELECT COUNT(*) FROM change_request WHERE state IN ('open', 'in_progress')";

ChangeRequestRecord readChange(const Row &row)
{
    ChangeRequestRecord record;
    record.id = std::string(viewOf(row.text(0)));
    parse(viewOf(row.text(1)), record.source);
    record.body = std::string(viewOf(row.text(2)));
    record.entryStep = static_cast<int32_t>(row.integer(3));
    parse(viewOf(row.text(4)), record.state);
    record.cpOpened = row.integer(5);
    if (!row.isNull(6)) { record.cpClosed = row.integer(6); }
    record.note = std::string(viewOf(row.text(7)));
    return record;
}

bool isClosing(KiottySpecChangeState state)
{
    return state == KIOTTY_SPEC_CHANGE_STATE_DONE || state == KIOTTY_SPEC_CHANGE_STATE_REJECTED;
}

void bindClosing(Query &query, int index, KiottySpecChangeState state)
{
    if (isClosing(state)) {
        query.bind(index, kPending);
        return;
    }
    query.bind(index, nullptr);
}

}

TextResult ChangeRequestStore::create(KiottySpecChangeSource source, std::string_view body, int32_t entryStep)
{
    const KiottySpecStatus validated = validate(source, body, entryStep);
    if (validated != KIOTTY_SPEC_STATUS_SUCCESS) { return TextResult::failure(validated); }
    auto id = _meta.nextId(ID_PREFIX, ID_WIDTH);
    if (!id) { return id; }
    const KiottySpecStatus inserted = insert(id.value, source, body, entryStep);
    if (inserted != KIOTTY_SPEC_STATUS_SUCCESS) { return TextResult::failure(inserted); }
    return id;
}

ChangeRequestResult ChangeRequestStore::get(std::string_view id)
{
    auto query = _storage.prepare(literalText(BY_ID_SQL));
    if (!query) { return ChangeRequestResult::failure(query.code); }
    query.value.bind(1, spanOf(id));
    const auto stepped = query.value.step();
    if (!stepped) { return ChangeRequestResult::failure(_storage.check(stepped.code)); }
    if (!stepped.value) { return ChangeRequestResult::failure(_storage.reject(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("변경 요청 없음: "), spanOf(id)})); }
    return ChangeRequestResult::success(readChange(query.value.row()));
}

KiottySpecStatus ChangeRequestStore::setState(std::string_view id, KiottySpecChangeState state, std::string_view note)
{
    const auto existing = get(id);
    if (!existing) { return existing.code; }
    auto query = _storage.prepare(literalText(SET_STATE_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, spanOf(id));
    query.value.bind(2, spanOf(name(state)));
    bindClosing(query.value, 3, state);
    query.value.bind(4, spanOf(note));
    return _storage.check(query.value.run());
}

KiottySpecStatus ChangeRequestStore::setEntryStep(std::string_view id, int32_t entryStep)
{
    const KiottySpecStatus stepChecked = checkStep(entryStep);
    if (stepChecked != KIOTTY_SPEC_STATUS_SUCCESS) { return stepChecked; }
    const auto existing = get(id);
    if (!existing) { return existing.code; }
    auto query = _storage.prepare(literalText(SET_ENTRY_STEP_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, spanOf(id));
    query.value.bind(2, static_cast<int64_t>(entryStep));
    return _storage.check(query.value.run());
}

ChangeRequestListResult ChangeRequestStore::list(KiottySpecChangeState stateOr0)
{
    auto query = _storage.prepare(literalText(LIST_SQL));
    if (!query) { return ChangeRequestListResult::failure(query.code); }
    const std::string_view state = stateOr0 != 0 ? name(stateOr0) : std::string_view();
    query.value.bind(1, spanOf(state));
    std::vector<ChangeRequestRecord> records;
    auto stepped = query.value.step();
    while (stepped.value) {
        records.push_back(readChange(query.value.row()));
        stepped = query.value.step();
    }
    if (!stepped) { return ChangeRequestListResult::failure(_storage.check(stepped.code)); }
    return ChangeRequestListResult::success(std::move(records));
}

CountResult ChangeRequestStore::openCount()
{
    auto query = _storage.prepare(literalText(OPEN_COUNT_SQL));
    if (!query) { return CountResult::failure(query.code); }
    const auto stepped = query.value.step();
    if (!stepped) { return CountResult::failure(_storage.check(stepped.code)); }
    return CountResult::success(static_cast<size_t>(query.value.row().integer(0)));
}

KiottySpecStatus ChangeRequestStore::validate(KiottySpecChangeSource source, std::string_view body, int32_t entryStep)
{
    const KiottySpecStatus stepChecked = checkStep(entryStep);
    if (stepChecked != KIOTTY_SPEC_STATUS_SUCCESS) { return stepChecked; }
    const bool isKnownSource = source == KIOTTY_SPEC_CHANGE_SOURCE_FEEDBACK || source == KIOTTY_SPEC_CHANGE_SOURCE_ISSUE;
    if (!isKnownSource) { return _storage.reject(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("알 수 없는 변경 요청 출처")}); }
    if (trim(body).empty()) { return _storage.reject(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("변경 요청 내용이 비어 있음")}); }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus ChangeRequestStore::checkStep(int32_t entryStep)
{
    const bool inRange = entryStep >= FIRST_STEP && entryStep <= LAST_STEP;
    if (inRange) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _storage.reject(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("진입 단계는 1 ~ 10 이어야 함")});
}

KiottySpecStatus ChangeRequestStore::insert(std::string_view id, KiottySpecChangeSource source, std::string_view body,
                                            int32_t entryStep)
{
    auto query = _storage.prepare(literalText(INSERT_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, spanOf(id));
    query.value.bind(2, spanOf(name(source)));
    query.value.bind(3, spanOf(body));
    query.value.bind(4, static_cast<int64_t>(entryStep));
    query.value.bind(5, kPending);
    return _storage.check(query.value.run());
}

}
