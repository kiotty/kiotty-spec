#include "implementation/data/repository/change_request_store.h"

#include "implementation/core/error.h"
#include "implementation/domain/entity/names.h"
#include "implementation/core/text.h"

namespace kiotty::spec::implementation {

namespace {

constexpr std::string_view kSelect = "SELECT id, source, body, entry_step, state, cp_opened, cp_closed, note FROM change_request ";

ChangeRequestRecord readChange(const Statement &stmt) {
    ChangeRequestRecord record;
    record.id = stmt.text(0);
    parse(stmt.text(1), record.source);
    record.body = stmt.text(2);
    record.entryStep = static_cast<int32_t>(stmt.integer(3));
    parse(stmt.text(4), record.state);
    record.cpOpened = stmt.integer(5);
    record.cpClosed = stmt.optInteger(6);
    record.note = stmt.text(7);
    return record;
}

void requireStep(int32_t step) {
    require(step >= 1 && step <= 10, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "진입 단계는 1 ~ 10 이어야 함");
}

}

std::string ChangeRequestStore::create(KiottySpecChangeSource source, std::string_view body, int32_t entryStep) {
    requireStep(entryStep);
    require(source == KIOTTY_SPEC_CHANGE_SOURCE_FEEDBACK || source == KIOTTY_SPEC_CHANGE_SOURCE_ISSUE,
                  KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "알 수 없는 변경 요청 출처");
    require(!trim(body).empty(), KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "변경 요청 내용이 비어 있음");
    const std::string id = meta_.nextId("CR", 3);
    db_.prepare("INSERT INTO change_request (id, source, body, entry_step, state, cp_opened) VALUES (?1, ?2, ?3, ?4, 'open', ?5)")
        .bindAll(id, name(source), body, entryStep, kPending)
        .run();
    return id;
}

ChangeRequestRecord ChangeRequestStore::get(std::string_view id) {
    auto stmt = db_.prepare(std::string(kSelect) + "WHERE id = ?1");
    stmt.bindAll(id);
    require(stmt.step(), KIOTTY_SPEC_STATUS_NOT_FOUND, "변경 요청 없음: " + std::string(id));
    return readChange(stmt);
}

void ChangeRequestStore::setState(std::string_view id, KiottySpecChangeState state, std::string_view note) {
    get(id);
    const bool closing = state == KIOTTY_SPEC_CHANGE_STATE_DONE || state == KIOTTY_SPEC_CHANGE_STATE_REJECTED;
    db_.prepare("UPDATE change_request SET state = ?2, cp_closed = ?3, note = CASE WHEN ?4 = '' THEN note ELSE ?4 END WHERE id = ?1")
        .bindAll(id, name(state), closing ? std::optional<int64_t>(kPending) : std::nullopt, note)
        .run();
}

void ChangeRequestStore::setEntryStep(std::string_view id, int32_t entryStep) {
    requireStep(entryStep);
    get(id);
    db_.prepare("UPDATE change_request SET entry_step = ?2 WHERE id = ?1").bindAll(id, entryStep).run();
}

std::vector<ChangeRequestRecord> ChangeRequestStore::list(KiottySpecChangeState stateOr0) {
    auto stmt = db_.prepare(std::string(kSelect) + "WHERE (?1 = '' OR state = ?1) ORDER BY id");
    stmt.bindAll(stateOr0 != 0 ? std::string(name(stateOr0)) : std::string());
    std::vector<ChangeRequestRecord> records;
    while (stmt.step()) {
        records.push_back(readChange(stmt));
    }
    return records;
}

size_t ChangeRequestStore::openCount() {
    auto stmt = db_.prepare("SELECT COUNT(*) FROM change_request WHERE state IN ('open', 'in_progress')");
    stmt.step();
    return static_cast<size_t>(stmt.integer(0));
}

}
