#include "implementation/domain/usecases/kiotty_legacy_importer.h"

#include "implementation/core/kiotty_decimal.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_text_view.h"
#include "implementation/core/kiotty_utf8.h"
#include "implementation/data/datasource/kiotty_jsonl_file.h"
#include "implementation/domain/usecases/kiotty_legacy_mapping.h"
#include "implementation/data/repository/kiotty_decision_store.h"

#include <limits>

namespace kiotty::spec::implementation {

namespace {

using entity::ItemKind;

constexpr int64_t LEGACY_PENDING = std::numeric_limits<int64_t>::max();
constexpr size_t MAX_NAME_LENGTH = 64;
constexpr size_t CHECKPOINT_DIGITS = 4;
constexpr std::string_view SKIPPED_TABLES[] = {"file_version", "change_request", "result", "milestone_state"};

using Number = FixedString<24>;

constexpr char META_SQL[] = "INSERT INTO meta (key, value) VALUES (?1, ?2)";
constexpr char ID_COUNTER_SQL[] = "INSERT INTO id_counter (prefix, next) VALUES (?1, ?2)";
constexpr char CHECKPOINT_SQL[] =
    "INSERT INTO checkpoint (sequence, created_at, previous, step, state, description) VALUES (?1, ?2, 0, ?3, ?4, ?5)";
// 정렬하지 않고 넣은 뒤, 바로 앞 sequence 를 previous 로 잇는다.
constexpr char LINK_PREVIOUS_SQL[] =
    "UPDATE checkpoint SET previous = COALESCE((SELECT MAX(c.sequence) FROM checkpoint c WHERE c.sequence < checkpoint.sequence), 0)";
constexpr char LATEST_SQL[] = "SELECT COALESCE(MAX(sequence), 0) FROM checkpoint";
constexpr char ITEM_SQL[] =
    "INSERT OR REPLACE INTO item_version (item_id, checkpoint, kind, parent_id, title, summary, body, tags, removed) "
    "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)";
constexpr char KIND_OF_SQL[] = "SELECT kind FROM item_version WHERE item_id = ?1 LIMIT 1";
constexpr char RELATION_SQL[] =
    "INSERT OR REPLACE INTO relation_version (from_id, model, relation, to_id, checkpoint, removed) VALUES (?1, ?2, ?3, ?4, ?5, ?6)";
constexpr char DECISION_SQL[] = "INSERT OR REPLACE INTO decision_version (kind, checkpoint, description) VALUES (?1, ?2, ?3)";

bool same(Span<const char> left, std::string_view right) { return viewOf(left) == right; }

Span<const char> keyOf(const char* key) { return boundedText(key, MAX_NAME_LENGTH); }

// 없거나 글자가 아니면 빈 글
Span<const char> textOf(JsonReader& row, const char* key, Span<char> scratch)
{
    const Result<Span<const char>, KiottySpecStatus> text = row.findText(keyOf(key), scratch);
    return text ? text.value : Span<const char>();
}

Number numberText(uint64_t value)
{
    Number text;
    appendDecimal(text, value);
    return text;
}

Number checkpointLabel(int64_t sequence)
{
    Number text;
    text.assign(literalText("CP-"));
    appendDecimal(text, sequence > 0 ? static_cast<uint64_t>(sequence) : 0, CHECKPOINT_DIGITS);
    return text;
}

}

Result<size_t, KiottySpecStatus> LegacyImporter::run(Span<const char> directory)
{
    using CountResult = Result<size_t, KiottySpecStatus>;
    KiottySpecStatus status = importMeta(directory);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = importIdCounters(directory); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = importCheckpoints(directory); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = importItems(directory); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = importLinks(directory); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = importDecisions(directory); }
    for (const std::string_view table : SKIPPED_TABLES) {
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = countSkipped(directory, table); }
    }
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(status); }
    return CountResult::success(_notes);
}

KiottySpecStatus LegacyImporter::eachRow(Span<const char> directory, std::string_view table, Insert insert)
{
    JsonlFile file(_areas.line, _areas.text);
    if (file.open(directory, table) != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _w.failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("jsonl 을 열 수 없음: "), spanOf(table)});
    }
    Result<bool, KiottySpecStatus> next = file.next();
    while (next && next.value) {
        const KiottySpecStatus status = (this->*insert)(file.row());
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
        next = file.next();
    }
    if (!next) { return _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("jsonl 을 읽을 수 없음: "), spanOf(table)}); }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus LegacyImporter::importMeta(Span<const char> directory) { return eachRow(directory, "meta", &LegacyImporter::insertMeta); }

KiottySpecStatus LegacyImporter::insertMeta(JsonReader& row)
{
    auto query = _w.storage.prepare(literalText(META_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, textOf(row, "key", _areas.text));
    query.value.bind(2, textOf(row, "value", _areas.text));
    return _w.storage.check(query.value.execute());
}

KiottySpecStatus LegacyImporter::importIdCounters(Span<const char> directory)
{
    return eachRow(directory, "id_counter", &LegacyImporter::insertIdCounter);
}

KiottySpecStatus LegacyImporter::insertIdCounter(JsonReader& row)
{
    const Span<const char> prefix = textOf(row, "prefix", _areas.first);
    if (prefix.length() == 0 || prefix[0] == '#' || same(prefix, "FILE")) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    auto query = _w.storage.prepare(literalText(ID_COUNTER_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, prefix);
    query.value.bind(2, row.findInteger(keyOf("next"), 1, _areas.text));
    return _w.storage.check(query.value.execute());
}

// 체크포인트: confirmed → DONE, 나머지 → RUNNING. note 는 63 바이트로 자른다.
KiottySpecStatus LegacyImporter::importCheckpoints(Span<const char> directory)
{
    const KiottySpecStatus status = eachRow(directory, "checkpoint", &LegacyImporter::insertCheckpoint);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    return linkCheckpoints();
}

KiottySpecStatus LegacyImporter::insertCheckpoint(JsonReader& row)
{
    const int64_t sequence = row.findInteger(keyOf("seq"), 0, _areas.text);
    const std::string_view purpose = viewOf(textOf(row, "purpose", _areas.first));
    const int64_t step = mapLegacyStep(row.findInteger(keyOf("step"), 1, _areas.text), purpose);
    const bool confirmed = row.findInteger(keyOf("confirmed"), 0, _areas.text) != 0;
    auto query = _w.storage.prepare(literalText(CHECKPOINT_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, sequence);
    query.value.bind(2, row.findInteger(keyOf("created_at"), 0, _areas.text));
    query.value.bind(3, step);
    query.value.bind(4, confirmed ? literalText("done") : literalText("running"));
    const Span<const char> description = textOf(row, "note", _areas.text);
    const Span<const char> cut = cutUtf8(description, entity::MAX_DESCRIPTION_LENGTH);
    if (cut.length() < description.length()) { note({checkpointLabel(sequence).view(), literalText(" 설명을 63 바이트로 자름: "), description}); }
    query.value.bind(5, cut);
    return _w.storage.check(query.value.execute());
}

KiottySpecStatus LegacyImporter::linkCheckpoints()
{
    const KiottySpecStatus linked = _w.storage.executeScript(literalText(LINK_PREVIOUS_SQL));
    if (linked != KIOTTY_SPEC_STATUS_SUCCESS) { return linked; }
    auto query = _w.storage.prepare(literalText(LATEST_SQL));
    if (!query) { return query.code; }
    const Result<bool, KiottySpecStatus> stepped = query.value.step();
    if (!stepped) { return _w.storage.check(stepped.code); }
    _latest = query.value.row().integer(0);
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

// 체크포인트에 묶이지 않은 쓰기(PENDING)는 가장 최근 체크포인트에 묶는다.
int64_t LegacyImporter::checkpointOf(JsonReader& row)
{
    const int64_t checkpoint = row.findInteger(keyOf("cp_seq"), _latest, _areas.text);
    return checkpoint == LEGACY_PENDING ? _latest : checkpoint;
}

// 항목: category → 태그, 제목이 127 바이트를 넘으면 자르고 원래 제목을 요약에 둔다.
KiottySpecStatus LegacyImporter::importItems(Span<const char> directory)
{
    const KiottySpecStatus status = eachRow(directory, "item_version", &LegacyImporter::insertItem);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS || _pending == 0) { return status; }
    note({literalText("체크포인트에 묶이지 않은 쓰기 "), numberText(_pending).view(), literalText(" 건을 가장 최근 체크포인트에 묶음")});
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus LegacyImporter::insertItem(JsonReader& row)
{
    const Span<const char> id = textOf(row, "item_id", _areas.first);
    const std::string_view kind = mapLegacyKind(textOf(row, "kind", _areas.second));
    if (kind.empty()) {
        note({id, literalText(" 알 수 없는 Kind 라 건너뜀")});
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }
    if (row.findInteger(keyOf("cp_seq"), 0, _areas.text) == LEGACY_PENDING) { ++_pending; }
    const ItemKind item_kind = static_cast<ItemKind>(entity::parseKind(kind));
    const LegacyTags tags = legacyTags(_w.catalog.finder(), item_kind, textOf(row, "category", _areas.second));
    auto query = _w.storage.prepare(literalText(ITEM_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, id);
    query.value.bind(2, checkpointOf(row));
    query.value.bind(3, spanOf(kind));
    const Result<Span<const char>, KiottySpecStatus> parent = row.findText(keyOf("parent_id"), _areas.second);
    if (parent) { query.value.bind(4, parent.value); } else { query.value.bind(4, nullptr); }
    const Span<const char> title = textOf(row, "title", _areas.text);
    const Span<const char> cut = cutUtf8(title, entity::MAX_TITLE_LENGTH);
    const bool shortened = cut.length() < title.length();
    query.value.bind(5, cut);
    query.value.bind(6, shortened ? cutUtf8(title, entity::MAX_SUMMARY_LENGTH) : literalText(""));
    if (shortened) { note({id, literalText(" 제목을 127 바이트로 자르고 원래 제목을 요약에 둠")}); }
    const Span<const char> body = textOf(row, "body", _areas.text);
    if (body.length() > entity::MAX_BODY_LENGTH) { note({id, literalText(" 본문을 16384 바이트로 자름")}); }
    query.value.bind(7, cutUtf8(body, entity::MAX_BODY_LENGTH));
    query.value.bind(8, tags.view());
    query.value.bind(9, row.findInteger(keyOf("removed"), 0, _areas.text));
    return _w.storage.check(query.value.execute());
}

// 링크 → 관계. 파일 링크는 파일 엔티티와 함께 없어진다 (7).
KiottySpecStatus LegacyImporter::importLinks(Span<const char> directory)
{
    const KiottySpecStatus status = eachRow(directory, "link_version", &LegacyImporter::insertLink);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS || _file_links == 0) { return status; }
    note({literalText("파일 엔티티와 이어진 링크 "), numberText(_file_links).view(), literalText(" 건을 지움 (설계서 v1 7)")});
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus LegacyImporter::insertLink(JsonReader& row)
{
    const Span<const char> from = textOf(row, "from_id", _areas.first);
    const Span<const char> to = textOf(row, "to_id", _areas.second);
    const std::string_view from_kind = kindOf(from);
    const std::string_view to_kind = kindOf(to);
    if (from_kind.empty() || to_kind.empty()) {
        ++_file_links;
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }
    const Span<const char> type = textOf(row, "type", _areas.text);
    const char* relation = legacyRelation(from_kind, type, to_kind);
    const ItemKind model = static_cast<ItemKind>(entity::parseKind(from_kind));
    const Result<uint32_t, KiottySpecStatus> index = relation == nullptr ? Result<uint32_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND)
                                                                         : _w.catalog.finder().findRelation(model, keyOf(relation));
    if (!index) {
        note({from, literalText(" -"), type, literalText("-> "), to, literalText(" 링크는 관계 정의에 없어 건너뜀")});
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }
    auto query = _w.storage.prepare(literalText(RELATION_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, from);
    query.value.bind(2, spanOf(from_kind));
    query.value.bind(3, static_cast<int64_t>(index.value));
    query.value.bind(4, to);
    query.value.bind(5, checkpointOf(row));
    query.value.bind(6, row.findInteger(keyOf("removed"), 0, _areas.text));
    return _w.storage.check(query.value.execute());
}

// 항목의 Kind 는 방금 넣은 item_version 에서 읽는다. 없으면 빈 글
std::string_view LegacyImporter::kindOf(Span<const char> id)
{
    auto query = _w.storage.prepare(literalText(KIND_OF_SQL));
    if (!query) { return std::string_view(); }
    query.value.bind(1, id);
    const Result<bool, KiottySpecStatus> stepped = query.value.step();
    if (!stepped || !stepped.value) { return std::string_view(); }
    const uint32_t kind = entity::parseKind(viewOf(query.value.row().text(0)));
    return kind == 0 ? std::string_view() : entity::kindName(static_cast<ItemKind>(kind));
}

KiottySpecStatus LegacyImporter::importDecisions(Span<const char> directory)
{
    return eachRow(directory, "decision_version", &LegacyImporter::insertDecision);
}

KiottySpecStatus LegacyImporter::insertDecision(JsonReader& row)
{
    const Span<const char> legacy_kind = textOf(row, "kind", _areas.first);
    const std::string_view kind = mapLegacyKind(legacy_kind);
    if (kind.empty()) {
        note({literalText("결정 기록 "), legacy_kind, literalText(" 는 Kind 를 알 수 없어 건너뜀")});
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }
    const int64_t checkpoint = checkpointOf(row);
    const Span<const char> text = textOf(row, "body", _areas.text);
    if (text.length() > MAX_DECISION_BYTES) {
        note({literalText("결정 기록 "), spanOf(kind), literalText(" ("), checkpointLabel(checkpoint).view(), literalText(") 을 2048 바이트로 자름 (원래 "),
              numberText(text.length()).view(), literalText(" 바이트)")});
    }
    auto query = _w.storage.prepare(literalText(DECISION_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, spanOf(kind));
    query.value.bind(2, checkpoint);
    query.value.bind(3, cutUtf8(text, MAX_DECISION_BYTES));
    return _w.storage.check(query.value.execute());
}

KiottySpecStatus LegacyImporter::countSkipped(Span<const char> directory, std::string_view table)
{
    _skipped_rows = 0;
    const KiottySpecStatus status = eachRow(directory, table, &LegacyImporter::countRow);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS || _skipped_rows == 0) { return status; }
    note({spanOf(table), literalText(" 의 "), numberText(_skipped_rows).view(), literalText(" 줄은 옮기지 않음")});
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus LegacyImporter::countRow(JsonReader&)
{
    ++_skipped_rows;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

// 보고 한 줄. 다 들어가지 않으면 빼고, 줄 수는 센다.
void LegacyImporter::note(std::initializer_list<Span<const char>> parts)
{
    ++_notes;
    size_t total = 3;
    for (const Span<const char> part : parts) { total += part.length(); }
    if (total > _report.capacity() - _report.view().length()) { return; }
    _report.append(literalText("- "));
    for (const Span<const char> part : parts) { _report.append(part); }
    _report.append(literalText("\n"));
}

}
