#include "implementation/domain/usecases/kiotty_exchange.h"

#include "implementation/core/kiotty_decimal.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_json_reader.h"
#include "implementation/core/kiotty_json_writer.h"
#include "implementation/core/kiotty_text_view.h"
#include "implementation/core/kiotty_utf8.h"
#include "implementation/data/datasource/kiotty_file_reader.h"
#include "implementation/data/datasource/kiotty_file_system.h"
#include "implementation/data/datasource/kiotty_file_writer.h"
#include "implementation/data/datasource/kiotty_savepoint.h"
#include "implementation/data/datasource/kiotty_schema.h"

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string_view>

namespace kiotty::spec::implementation {

namespace {

using entity::ItemKind;

constexpr char JSONL_EXTENSION[] = ".jsonl";
constexpr int64_t LEGACY_PENDING = std::numeric_limits<int64_t>::max();
// 정의서 v0 에만 있던 파일. 내보낼 때 지운다.
constexpr std::string_view LEGACY_FILES[] = {"link_version", "file_version", "milestone_state", "change_request", "result"};
constexpr size_t MAX_COLUMNS = 32;
// 작업 버퍼에서 ID · Kind 같은 짧은 값에 주는 칸
constexpr size_t SHORT_AREA_BYTES = 1024;
constexpr size_t MAX_TABLE_NAME = 64;
constexpr size_t MAX_SQL_LENGTH = 1024;
constexpr size_t MAX_TAGS_TEXT = 64;
constexpr size_t CHECKPOINT_DIGITS = 4;

using SqlText = FixedString<MAX_SQL_LENGTH>;

SystemPathResult tableFile(Span<const char> directory, std::string_view table)
{
    FixedString<MAX_TABLE_NAME> name;
    if (name.assign(spanOf(table)) != KIOTTY_SPEC_STATUS_SUCCESS || name.append(literalText(JSONL_EXTENSION)) != KIOTTY_SPEC_STATUS_SUCCESS) {
        return SystemPathResult::failure(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED);
    }
    return joinPath(directory, name.view());
}

bool same(Span<const char> left, std::string_view right) { return viewOf(left) == right; }

bool isColumnName(Span<const char> name)
{
    if (name.length() == 0) { return false; }
    for (const char letter : name) {
        if (!((letter >= 'a' && letter <= 'z') || letter == '_')) { return false; }
    }
    return true;
}

// nlohmann::json 의 get<int64_t>() 와 같게 읽는다. 없거나 숫자가 아니면 fallback
int64_t integerOf(JsonReader& reader, const char* key, int64_t fallback, Span<char> scratch)
{
    JsonField field;
    const auto found = reader.find(boundedText(key, MAX_TABLE_NAME), field, scratch);
    if (!found || !found.value) { return fallback; }
    switch (field.type) {
    case JsonType::INTEGER: return field.integer;
    case JsonType::UNSIGNED: return static_cast<int64_t>(field.natural);
    case JsonType::BOOLEAN: return field.boolean ? 1 : 0;
    default: return fallback;
    }
}

// 글자 값. 없거나 글자가 아니면 빈 글. present 는 글자 값이 있었는지
Span<const char> textOf(JsonReader& reader, const char* key, Span<char> scratch, bool* present = nullptr)
{
    JsonField field;
    const auto found = reader.find(boundedText(key, MAX_TABLE_NAME), field, scratch);
    const bool text = found && found.value && field.type == JsonType::TEXT;
    if (present != nullptr) { *present = text; }
    return text ? field.text : Span<const char>();
}

KiottySpecStatus bindField(Statement& query, int index, const JsonField& field)
{
    switch (field.type) {
    case JsonType::NULL_VALUE: return query.bind(index, nullptr);
    case JsonType::BOOLEAN: return query.bind(index, static_cast<int64_t>(field.boolean ? 1 : 0));
    case JsonType::INTEGER: return query.bind(index, field.integer);
    case JsonType::UNSIGNED: return query.bind(index, static_cast<int64_t>(field.natural));
    case JsonType::REAL: return query.bind(index, field.real);
    case JsonType::TEXT:
    case JsonType::COMPOSITE: return query.bind(index, field.text);
    }
    return KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
}

// UTF-8 글자 경계에서 limit 바이트 이하로 자른다.
Span<const char> cutUtf8(Span<const char> text, size_t limit)
{
    if (text.length() <= limit) { return text; }
    size_t cut = limit;
    while (cut > 0 && isUtf8Continuation(text[cut])) { --cut; }
    return Span<const char>(text.data(), cut);
}

// 태그는 DB 에 "[[1,4]]" 글자로 두고, jsonl 에는 배열로 쓴다 (3.6). 배열 모양이 아니면 [].
template <typename Text>
KiottySpecStatus appendTags(Text& text, Span<const char> tags)
{
    bool valid = tags.length() >= 2 && tags[0] == '[' && tags[tags.length() - 1] == ']';
    for (const char letter : tags) {
        const bool allowed = letter == '[' || letter == ']' || letter == ',' || letter == '-' || letter == ' ' || (letter >= '0' && letter <= '9');
        valid = valid && allowed;
    }
    if (!valid) { return text.append(literalText("[]")); }
    KiottySpecStatus status = KIOTTY_SPEC_STATUS_SUCCESS;
    for (size_t index = 0; index < tags.length() && status == KIOTTY_SPEC_STATUS_SUCCESS; ++index) {
        if (tags[index] != ' ') { status = text.append(Span<const char>(&tags[index], 1)); }
    }
    return status;
}

// 열 이름 순으로 객체 한 줄을 쓴다 (nlohmann::json 의 키 순서와 같다).
KiottySpecStatus appendRow(TextBuffer& line, const Row& row, const int (&order)[MAX_COLUMNS], int count, bool item_version)
{
    KiottySpecStatus status = line.append(literalText("{"));
    for (int position = 0; position < count && status == KIOTTY_SPEC_STATUS_SUCCESS; ++position) {
        const int column = order[position];
        if (position > 0) { status = line.append(literalText(",")); }
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = appendJsonString(line, row.name(column)); }
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = line.append(literalText(":")); }
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { break; }
        switch (row.type(column)) {
        case ColumnType::INTEGER: status = appendJsonInteger(line, row.integer(column)); break;
        case ColumnType::FLOAT: status = appendJsonReal(line, row.real(column)); break;
        case ColumnType::NULL_VALUE: status = line.append(literalText("null")); break;
        case ColumnType::TEXT:
        case ColumnType::BLOB:
            if (item_version && same(row.name(column), "tags")) {
                status = appendTags(line, row.text(column));
            } else {
                status = appendJsonString(line, row.text(column));
            }
            break;
        }
    }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = line.append(literalText("}\n")); }
    return status;
}

// 보고 한 줄. 다 들어가지 않으면 빼고, 줄 수는 센다.
void note(SpanText& report, size_t& notes, std::initializer_list<Span<const char>> parts)
{
    ++notes;
    size_t total = 3;
    for (const Span<const char> part : parts) { total += part.length(); }
    if (total > report.capacity() - report.view().length()) { return; }
    report.append(literalText("- "));
    for (const Span<const char> part : parts) { report.append(part); }
    report.append(literalText("\n"));
}

using Number = FixedString<24>;

Number numberText(uint64_t value, size_t min_digits = 1)
{
    Number text;
    appendDecimal(text, value, min_digits);
    return text;
}

Number checkpointLabel(int64_t sequence)
{
    Number text;
    text.assign(literalText("CP-"));
    appendDecimal(text, sequence > 0 ? static_cast<uint64_t>(sequence) : 0, CHECKPOINT_DIGITS);
    return text;
}

// 정의서 v0 의 단계 번호 → 설계서 v1 (11절)
int64_t mapLegacyStep(int64_t step, bool release)
{
    if (step <= 2) { return 2; }
    if (step <= 8) { return step; }
    if (step == 9) { return 10; }
    return release ? 12 : 11;
}

struct LegacyKind {
    std::string_view legacy;
    std::string_view kind;
};

constexpr LegacyKind LEGACY_KINDS[] = {
    {"requirement", "requirement"},       {"rule", "rule"},       {"interface", "external_interface"}, {"design", "design"},
    {"unit_test", "unit_test_plan"},      {"system_test", "system_test_plan"}, {"e2e_test", "e2e_test_plan"},
    {"milestone", "milestone"},           {"plan", "milestone"},
};

// 없으면 빈 글
std::string_view mapLegacyKind(Span<const char> kind)
{
    for (const LegacyKind& entry : LEGACY_KINDS) {
        if (same(kind, entry.legacy)) { return entry.kind; }
    }
    return std::string_view();
}

struct LegacyCategory {
    const char* category;
    const char* tag;
    const char* bit;
};

constexpr LegacyCategory LEGACY_CATEGORIES[] = {
    {"functional", "요구사항 종류", "기능 요구사항"}, {"quality", "요구사항 종류", "품질 요구사항"},
    {"environment", "요구사항 종류", "환경 요구사항"}, {"architecture", "수칙 종류", "아키텍처"},
    {"convention", "수칙 종류", "컨벤션"},          {"coding", "수칙 종류", "코딩 제약"},
    {"dependency", "수칙 종류", "의존성"},          {"test", "수칙 종류", "테스트"},
    {"view", "인터페이스 종류", "GUI 뷰"},           {"c_api", "인터페이스 종류", "C 함수"},
    {"module", "설계 종류", "모듈"},                 {"flow", "설계 종류", "동작 흐름"},
    {"directory", "설계 종류", "디렉토리"},          {"memory", "설계 종류", "메모리 정책"},
};

struct LegacyLink {
    std::string_view from_kind;
    std::string_view type;
    std::string_view to_kind;
    const char* relation;
};

constexpr LegacyLink LEGACY_LINKS[] = {
    {"requirement", "satisfies", "requirement", "관련 요구사항"},
    {"external_interface", "satisfies", "requirement", "관련 요구사항"},
    {"design", "satisfies", "requirement", "관련 요구사항"},
    {"design", "satisfies", "external_interface", "관련 외부 인터페이스"},
    {"design", "depends_on", "design", "의존 설계 사항"},
    {"unit_test_plan", "verifies", "design", "검증 설계 사항"},
    {"system_test_plan", "verifies", "requirement", "검증 요구사항"},
    {"e2e_test_plan", "verifies", "external_interface", "검증 외부 인터페이스"},
    {"milestone", "covers", "design", "포함 설계 사항"},
    {"milestone", "covers", "unit_test_plan", "포함 유닛 테스트"},
};

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

}

Exchange::Areas Exchange::areas()
{
    const Span<char> all = _work.scratch();
    const size_t half = all.length() / 2;
    const size_t rest = all.length() - half;
    const size_t small = rest / 4 < SHORT_AREA_BYTES ? rest / 4 : SHORT_AREA_BYTES;
    Areas areas;
    areas.line = Span<char>(all.data(), half);
    areas.first = Span<char>(all.data() + half, small);
    areas.second = Span<char>(all.data() + half + small, small);
    areas.text = Span<char>(all.data() + half + small * 2, rest - small * 2);
    return areas;
}

template <typename Visit>
KiottySpecStatus Exchange::eachRow(Span<const char> directory, std::string_view table, const Areas& areas, Visit visit)
{
    const auto path = tableFile(directory, table);
    if (!path) { return _w.failure.raise(path.code, {literalText("경로가 너무 김: "), directory}); }
    if (!isFile(path.value.view())) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    FileReader file(areas.line);
    if (file.open(path.value.view()) != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _w.failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("파일을 열 수 없음: "), path.value.view()});
    }
    while (true) {
        const auto read = file.next();
        if (!read) { return _w.failure.raise(read.code, {literalText("jsonl 을 읽을 수 없음: "), spanOf(table)}); }
        if (!read.value) { return KIOTTY_SPEC_STATUS_SUCCESS; }
        if (file.line().length() == 0) { continue; }
        // 줄 전체가 객체인지 먼저 확인한다.
        JsonReader reader(file.line());
        JsonField field;
        while (true) {
            const auto next = reader.next(field, areas.text);
            if (!next) { return _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("jsonl 을 읽을 수 없음: "), spanOf(table)}); }
            if (!next.value) { break; }
        }
        reader.rewind();
        const KiottySpecStatus status = visit(reader);
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    }
}

KiottySpecStatus Exchange::exportJsonl(Span<const char> directory)
{
    if (createDirectories(directory) != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _w.failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("디렉토리를 만들 수 없음: "), directory});
    }
    for (const OrderedTable& table : orderedTables()) {
        SqlText sql;
        sql.assign(literalText("SELECT * FROM "));
        sql.append(spanOf(table.name));
        sql.append(literalText(" ORDER BY "));
        if (sql.append(spanOf(table.key_order)) != KIOTTY_SPEC_STATUS_SUCCESS) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
        auto query = _w.storage.prepare(sql.view());
        if (!query) { return query.code; }
        const Row row = query.value.row();
        const int count = row.count();
        if (count < 0 || static_cast<size_t>(count) > MAX_COLUMNS) {
            return _w.failure.raise(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, {literalText("열이 너무 많음: "), spanOf(table.name)});
        }
        int order[MAX_COLUMNS] {};
        for (int column = 0; column < count; ++column) {
            int position = column;
            while (position > 0 && viewOf(row.name(order[position - 1])) > viewOf(row.name(column))) {
                order[position] = order[position - 1];
                --position;
            }
            order[position] = column;
        }
        const auto path = tableFile(directory, table.name);
        if (!path) { return _w.failure.raise(path.code, {literalText("경로가 너무 김: "), directory}); }
        FileWriter file;
        if (file.open(path.value.view()) != KIOTTY_SPEC_STATUS_SUCCESS) {
            return _w.failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("파일을 쓸 수 없음: "), path.value.view()});
        }
        const bool item_version = table.name == "item_version";
        while (true) {
            const auto stepped = query.value.step();
            if (!stepped) { return _w.storage.check(stepped.code); }
            if (!stepped.value) { break; }
            _work.clear();
            if (appendRow(_work, query.value.row(), order, count, item_version) != KIOTTY_SPEC_STATUS_SUCCESS) {
                return _w.failure.raise(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, {literalText("한 줄이 작업 버퍼보다 큼: "), spanOf(table.name)});
            }
            if (file.write(_work.view()) != KIOTTY_SPEC_STATUS_SUCCESS) {
                return _w.failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("파일을 쓸 수 없음: "), path.value.view()});
            }
        }
    }
    for (const std::string_view legacy : LEGACY_FILES) {
        const auto path = tableFile(directory, legacy);
        if (path) { removeFile(path.value.view()); }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<size_t, KiottySpecStatus> Exchange::importJsonl(Span<const char> directory, Span<char> report_area)
{
    using CountResult = Result<size_t, KiottySpecStatus>;
    SpanText report(report_area);
    if (!isDirectory(directory)) { return CountResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("jsonl 디렉토리가 없음: "), directory})); }
    const auto legacyMarker = tableFile(directory, "link_version");
    const bool legacy = legacyMarker && isFile(legacyMarker.value.view());
    const Areas work = areas();
    size_t notes = 0;
    KiottySpecStatus status = KIOTTY_SPEC_STATUS_SUCCESS;
    {
        Savepoint savepoint(_w.storage.connection());
        status = _w.storage.check(savepoint.begin());
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = recreate(_w.storage); }
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = legacy ? importLegacy(directory, work, report, notes) : importCurrent(directory, work); }
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = _w.storage.check(savepoint.commit()); }
    }
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        _w.reload();
        return CountResult::failure(status);
    }
    const KiottySpecStatus reloaded = _w.reload();
    if (reloaded != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(reloaded); }
    return CountResult::success(notes);
}

KiottySpecStatus Exchange::importCurrent(Span<const char> directory, const Areas& areas)
{
    for (const OrderedTable& table : orderedTables()) {
        const KiottySpecStatus status = eachRow(directory, table.name, areas, [&](JsonReader& reader) -> KiottySpecStatus {
            SqlText sql;
            SqlText values;
            sql.assign(literalText("INSERT INTO "));
            sql.append(spanOf(table.name));
            sql.append(literalText(" ("));
            JsonField field;
            uint64_t index = 0;
            while (true) {
                const auto next = reader.next(field, areas.text);
                if (!next) { return next.code; }
                if (!next.value) { break; }
                if (!isColumnName(field.key)) { return _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("알 수 없는 열: "), field.key}); }
                ++index;
                KiottySpecStatus built = sql.append(index == 1 ? literalText("") : literalText(", "));
                if (built == KIOTTY_SPEC_STATUS_SUCCESS) { built = sql.append(field.key); }
                if (built == KIOTTY_SPEC_STATUS_SUCCESS) { built = values.append(index == 1 ? literalText("?") : literalText(", ?")); }
                if (built == KIOTTY_SPEC_STATUS_SUCCESS) { built = appendDecimal(values, index); }
                if (built != KIOTTY_SPEC_STATUS_SUCCESS) { return _w.failure.raise(built, {literalText("열이 너무 많음: "), spanOf(table.name)}); }
            }
            sql.append(literalText(") VALUES ("));
            sql.append(values.view());
            if (sql.append(literalText(")")) != KIOTTY_SPEC_STATUS_SUCCESS) { return _w.failure.raise(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, {literalText("열이 너무 많음: "), spanOf(table.name)}); }
            auto query = _w.storage.prepare(sql.view());
            if (!query) { return query.code; }
            reader.rewind();
            int position = 1;
            while (true) {
                const auto next = reader.next(field, areas.text);
                if (!next) { return next.code; }
                if (!next.value) { break; }
                bindField(query.value, position++, field);
            }
            return _w.storage.check(query.value.execute());
        });
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus Exchange::importLegacy(Span<const char> directory, const Areas& areas, SpanText& report, size_t& notes)
{
    auto prepare = [&](const auto& sql) { return _w.storage.prepare(literalText(sql)); };

    KiottySpecStatus status = eachRow(directory, "meta", areas, [&](JsonReader& row) -> KiottySpecStatus {
        auto query = prepare("INSERT INTO meta (key, value) VALUES (?1, ?2)");
        if (!query) { return query.code; }
        query.value.bind(1, textOf(row, "key", areas.text));
        query.value.bind(2, textOf(row, "value", areas.text));
        return _w.storage.check(query.value.execute());
    });
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }

    status = eachRow(directory, "id_counter", areas, [&](JsonReader& row) -> KiottySpecStatus {
        const Span<const char> prefix = textOf(row, "prefix", areas.first);
        if (prefix.length() == 0 || prefix[0] == '#' || same(prefix, "FILE")) { return KIOTTY_SPEC_STATUS_SUCCESS; }
        auto query = prepare("INSERT INTO id_counter (prefix, next) VALUES (?1, ?2)");
        if (!query) { return query.code; }
        query.value.bind(1, prefix);
        query.value.bind(2, integerOf(row, "next", 1, areas.text));
        return _w.storage.check(query.value.execute());
    });
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }

    // 체크포인트: confirmed → DONE, 나머지 → RUNNING. note 는 63 바이트로 자른다.
    status = eachRow(directory, "checkpoint", areas, [&](JsonReader& row) -> KiottySpecStatus {
        const int64_t sequence = integerOf(row, "seq", 0, areas.text);
        const bool release = same(textOf(row, "purpose", areas.first), "release");
        auto query = prepare(CHECKPOINT_SQL);
        if (!query) { return query.code; }
        query.value.bind(1, sequence);
        query.value.bind(2, integerOf(row, "created_at", 0, areas.text));
        query.value.bind(3, mapLegacyStep(integerOf(row, "step", 1, areas.text), release));
        query.value.bind(4, integerOf(row, "confirmed", 0, areas.text) != 0 ? literalText("done") : literalText("running"));
        const Span<const char> description = textOf(row, "note", areas.text);
        const Span<const char> cut = cutUtf8(description, entity::MAX_DESCRIPTION_LENGTH);
        if (cut.length() < description.length()) { note(report, notes, {checkpointLabel(sequence).view(), literalText(" 설명을 63 바이트로 자름: "), description}); }
        query.value.bind(5, cut);
        return _w.storage.check(query.value.execute());
    });
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    status = _w.storage.executeScript(literalText(LINK_PREVIOUS_SQL));
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    int64_t latest = 0;
    {
        auto query = prepare(LATEST_SQL);
        if (!query) { return query.code; }
        const auto stepped = query.value.step();
        if (!stepped) { return _w.storage.check(stepped.code); }
        latest = query.value.row().integer(0);
    }

    // 항목: category → 태그, 제목이 127 바이트를 넘으면 자르고 원래 제목을 요약에 둔다.
    size_t pending = 0;
    status = eachRow(directory, "item_version", areas, [&](JsonReader& row) -> KiottySpecStatus {
        const Span<const char> id = textOf(row, "item_id", areas.first);
        const std::string_view kind = mapLegacyKind(textOf(row, "kind", areas.second));
        if (kind.empty()) {
            note(report, notes, {id, literalText(" 알 수 없는 Kind 라 건너뜀")});
            return KIOTTY_SPEC_STATUS_SUCCESS;
        }
        const ItemKind item_kind = static_cast<ItemKind>(entity::parseKind(kind));
        int64_t checkpoint = integerOf(row, "cp_seq", latest, areas.text);
        if (checkpoint == LEGACY_PENDING) {
            checkpoint = latest;
            ++pending;
        }
        FixedString<MAX_TAGS_TEXT> tags;
        tags.assign(literalText("["));
        const Span<const char> category = textOf(row, "category", areas.second);
        for (const LegacyCategory& entry : LEGACY_CATEGORIES) {
            if (!same(category, entry.category)) { continue; }
            const auto tag = _w.catalog.finder().findTag(item_kind, boundedText(entry.tag, MAX_TAGS_TEXT));
            if (!tag) { break; }
            const auto bit = _w.catalog.finder().findTagBit(item_kind, tag.value, boundedText(entry.bit, MAX_TAGS_TEXT));
            if (bit) {
                tags.append(literalText("["));
                appendDecimal(tags, tag.value);
                tags.append(literalText(","));
                appendDecimal(tags, uint64_t{1} << bit.value);
                tags.append(literalText("]"));
            }
            break;
        }
        tags.append(literalText("]"));

        auto query = prepare(ITEM_SQL);
        if (!query) { return query.code; }
        query.value.bind(1, id);
        query.value.bind(2, checkpoint);
        query.value.bind(3, spanOf(kind));
        bool has_parent = false;
        const Span<const char> parent = textOf(row, "parent_id", areas.second, &has_parent);
        if (has_parent) { query.value.bind(4, parent); } else { query.value.bind(4, nullptr); }
        const Span<const char> title = textOf(row, "title", areas.text);
        const Span<const char> cut = cutUtf8(title, entity::MAX_TITLE_LENGTH);
        query.value.bind(5, cut);
        if (cut.length() < title.length()) {
            query.value.bind(6, cutUtf8(title, entity::MAX_SUMMARY_LENGTH));
            note(report, notes, {id, literalText(" 제목을 127 바이트로 자르고 원래 제목을 요약에 둠")});
        } else {
            query.value.bind(6, literalText(""));
        }
        const Span<const char> body = textOf(row, "body", areas.text);
        if (body.length() > entity::MAX_BODY_LENGTH) { note(report, notes, {id, literalText(" 본문을 16384 바이트로 자름")}); }
        query.value.bind(7, cutUtf8(body, entity::MAX_BODY_LENGTH));
        query.value.bind(8, tags.view());
        query.value.bind(9, integerOf(row, "removed", 0, areas.text));
        return _w.storage.check(query.value.execute());
    });
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    if (pending > 0) { note(report, notes, {literalText("체크포인트에 묶이지 않은 쓰기 "), numberText(pending).view(), literalText(" 건을 가장 최근 체크포인트에 묶음")}); }

    // 링크 → 관계. 파일 링크는 파일 엔티티와 함께 없어진다 (7). 항목의 Kind 는 방금 넣은 item_version 에서 읽는다.
    auto kindOf = [&](Span<const char> id) -> std::string_view {
        auto query = prepare(KIND_OF_SQL);
        if (!query) { return std::string_view(); }
        query.value.bind(1, id);
        const auto stepped = query.value.step();
        if (!stepped || !stepped.value) { return std::string_view(); }
        const uint32_t kind = entity::parseKind(viewOf(query.value.row().text(0)));
        return kind == 0 ? std::string_view() : entity::kindName(static_cast<ItemKind>(kind));
    };
    size_t file_links = 0;
    status = eachRow(directory, "link_version", areas, [&](JsonReader& row) -> KiottySpecStatus {
        const Span<const char> from = textOf(row, "from_id", areas.first);
        const Span<const char> to = textOf(row, "to_id", areas.second);
        const std::string_view from_kind = kindOf(from);
        const std::string_view to_kind = kindOf(to);
        if (from_kind.empty() || to_kind.empty()) {
            ++file_links;
            return KIOTTY_SPEC_STATUS_SUCCESS;
        }
        const Span<const char> type = textOf(row, "type", areas.text);
        const char* relation = nullptr;
        for (const LegacyLink& link : LEGACY_LINKS) {
            if (from_kind == link.from_kind && same(type, link.type) && to_kind == link.to_kind) { relation = link.relation; }
        }
        const ItemKind model = static_cast<ItemKind>(entity::parseKind(from_kind));
        const auto index = relation == nullptr ? Result<uint32_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND)
                                               : _w.catalog.finder().findRelation(model, boundedText(relation, MAX_TAGS_TEXT));
        if (!index) {
            note(report, notes, {from, literalText(" -"), type, literalText("-> "), to, literalText(" 링크는 관계 정의에 없어 건너뜀")});
            return KIOTTY_SPEC_STATUS_SUCCESS;
        }
        int64_t checkpoint = integerOf(row, "cp_seq", latest, areas.text);
        if (checkpoint == LEGACY_PENDING) { checkpoint = latest; }
        auto query = prepare(RELATION_SQL);
        if (!query) { return query.code; }
        query.value.bind(1, from);
        query.value.bind(2, spanOf(from_kind));
        query.value.bind(3, static_cast<int64_t>(index.value));
        query.value.bind(4, to);
        query.value.bind(5, checkpoint);
        query.value.bind(6, integerOf(row, "removed", 0, areas.text));
        return _w.storage.check(query.value.execute());
    });
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    if (file_links > 0) { note(report, notes, {literalText("파일 엔티티와 이어진 링크 "), numberText(file_links).view(), literalText(" 건을 지움 (설계서 v1 7)")}); }

    status = eachRow(directory, "decision_version", areas, [&](JsonReader& row) -> KiottySpecStatus {
        const Span<const char> legacy_kind = textOf(row, "kind", areas.first);
        const std::string_view kind = mapLegacyKind(legacy_kind);
        if (kind.empty()) {
            note(report, notes, {literalText("결정 기록 "), legacy_kind, literalText(" 는 Kind 를 알 수 없어 건너뜀")});
            return KIOTTY_SPEC_STATUS_SUCCESS;
        }
        int64_t checkpoint = integerOf(row, "cp_seq", latest, areas.text);
        if (checkpoint == LEGACY_PENDING) { checkpoint = latest; }
        const Span<const char> text = textOf(row, "body", areas.text);
        if (text.length() > MAX_DECISION_BYTES) {
            note(report, notes, {literalText("결정 기록 "), spanOf(kind), literalText(" ("), checkpointLabel(checkpoint).view(),
                                 literalText(") 을 2048 바이트로 자름 (원래 "), numberText(text.length()).view(), literalText(" 바이트)")});
        }
        auto query = prepare(DECISION_SQL);
        if (!query) { return query.code; }
        query.value.bind(1, spanOf(kind));
        query.value.bind(2, checkpoint);
        query.value.bind(3, cutUtf8(text, MAX_DECISION_BYTES));
        return _w.storage.check(query.value.execute());
    });
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }

    const std::string_view skipped[] = {"file_version", "change_request", "result", "milestone_state"};
    for (const std::string_view table : skipped) {
        size_t rows = 0;
        status = eachRow(directory, table, areas, [&](JsonReader&) -> KiottySpecStatus {
            ++rows;
            return KIOTTY_SPEC_STATUS_SUCCESS;
        });
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
        if (rows > 0) { note(report, notes, {spanOf(table), literalText(" 의 "), numberText(rows).view(), literalText(" 줄은 옮기지 않음")}); }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

}
