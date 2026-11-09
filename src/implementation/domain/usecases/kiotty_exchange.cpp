#include "implementation/domain/usecases/kiotty_exchange.h"

#include "implementation/core/kiotty_text_view.h"
#include "implementation/core/kiotty_utf8.h"
#include "implementation/data/datasource/kiotty_file_system.h"
#include "implementation/data/datasource/kiotty_savepoint.h"
#include "implementation/data/datasource/kiotty_schema.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <string_view>
#include <vector>

namespace kiotty::spec::implementation {

using nlohmann::json;

namespace {

using entity::ItemKind;

constexpr char JSONL_EXTENSION[] = ".jsonl";
constexpr int64_t LEGACY_PENDING = std::numeric_limits<int64_t>::max();
// 정의서 v0 에만 있던 파일. 내보낼 때 지운다.
constexpr std::string_view LEGACY_FILES[] = {"link_version", "file_version", "milestone_state", "change_request", "result"};

SystemPathResult tableFile(Span<const char> directory, std::string_view table)
{
    std::string name(table);
    name += JSONL_EXTENSION;
    return joinPath(directory, spanOf(name));
}

std::string dumpLine(const json& value)
{
    return value.dump(-1, ' ', false, json::error_handler_t::replace) + "\n";
}

bool isColumnName(std::string_view name)
{
    if (name.empty()) { return false; }
    for (const char letter : name) {
        if (!((letter >= 'a' && letter <= 'z') || letter == '_')) { return false; }
    }
    return true;
}

int64_t integerOf(const json& object, const char* key, int64_t fallback)
{
    const auto found = object.find(key);
    if (found == object.end()) { return fallback; }
    if (found->is_number_integer()) { return found->get<int64_t>(); }
    if (found->is_number_unsigned()) {
        const uint64_t value = found->get<uint64_t>();
        return value > static_cast<uint64_t>(LEGACY_PENDING) ? LEGACY_PENDING : static_cast<int64_t>(value);
    }
    if (found->is_boolean()) { return found->get<bool>() ? 1 : 0; }
    return fallback;
}

std::string textOf(const json& object, const char* key)
{
    const auto found = object.find(key);
    if (found == object.end() || !found->is_string()) { return std::string(); }
    return found->get<std::string>();
}

bool hasText(const json& object, const char* key)
{
    const auto found = object.find(key);
    return found != object.end() && found->is_string();
}

// UTF-8 글자 경계에서 limit 바이트 이하로 자른다.
std::string cutUtf8(const std::string& text, size_t limit)
{
    if (text.size() <= limit) { return text; }
    size_t cut = limit;
    while (cut > 0 && isUtf8Continuation(text[cut])) { --cut; }
    return text.substr(0, cut);
}

// 파일을 줄마다 json 객체로 읽는다. 파일이 없으면 빈 목록. 깨진 줄이 있으면 false
bool readObjects(Span<const char> directory, std::string_view table, std::vector<json>& rows, bool& present)
{
    rows.clear();
    const auto path = tableFile(directory, table);
    present = path && isFile(path.value.view());
    if (!present) { return true; }
    const auto content = readWholeFile(path.value.view());
    if (!content) { return false; }
    size_t start = 0;
    while (start < content->size()) {
        size_t end = content->find('\n', start);
        if (end == std::string::npos) { end = content->size(); }
        std::string_view line(content->data() + start, end - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) { line.remove_suffix(1); }
        if (!line.empty()) {
            json row = json::parse(line, nullptr, false);
            if (row.is_discarded() || !row.is_object()) { return false; }
            rows.push_back(std::move(row));
        }
        start = end + 1;
    }
    return true;
}

KiottySpecStatus bindValue(Statement& query, int index, const json& value)
{
    if (value.is_null()) { return query.bind(index, nullptr); }
    if (value.is_boolean()) { return query.bind(index, static_cast<int64_t>(value.get<bool>() ? 1 : 0)); }
    if (value.is_number_integer()) { return query.bind(index, value.get<int64_t>()); }
    if (value.is_number_unsigned()) { return query.bind(index, static_cast<int64_t>(value.get<uint64_t>())); }
    if (value.is_number()) { return query.bind(index, value.get<double>()); }
    if (value.is_string()) {
        const std::string& text = value.get_ref<const std::string&>();
        return query.bind(index, spanOf(text));
    }
    const std::string text = value.dump(-1, ' ', false, json::error_handler_t::replace);
    return query.bind(index, spanOf(text));
}

json rowObject(const Row& row, std::string_view table)
{
    json object = json::object();
    for (int column = 0; column < row.count(); ++column) {
        const std::string key(viewOf(row.name(column)));
        switch (row.type(column)) {
        case ColumnType::INTEGER: object[key] = row.integer(column); break;
        case ColumnType::FLOAT: object[key] = row.real(column); break;
        case ColumnType::NULL_VALUE: object[key] = nullptr; break;
        case ColumnType::TEXT:
        case ColumnType::BLOB: object[key] = std::string(viewOf(row.text(column))); break;
        }
    }
    // 태그는 DB 에 "[[1,4]]" 글자로 두고, jsonl 에는 배열로 쓴다 (3.6).
    if (table == "item_version" && object["tags"].is_string()) {
        json tags = json::parse(object["tags"].get<std::string>(), nullptr, false);
        object["tags"] = tags.is_discarded() ? json::array() : tags;
    }
    return object;
}

void note(std::string& report, size_t& notes, const std::string& line)
{
    report += "- ";
    report += line;
    report += "\n";
    ++notes;
}

std::string checkpointLabel(int64_t sequence)
{
    std::string digits = std::to_string(sequence);
    while (digits.size() < 4) { digits.insert(digits.begin(), '0'); }
    return "CP-" + digits;
}

// 정의서 v0 의 단계 번호 → 설계서 v1 (11절)
int64_t mapLegacyStep(int64_t step, bool release)
{
    if (step <= 2) { return 2; }
    if (step <= 8) { return step; }
    if (step == 9) { return 10; }
    return release ? 12 : 11;
}

const char* mapLegacyKind(const std::string& kind)
{
    if (kind == "requirement") { return "requirement"; }
    if (kind == "rule") { return "rule"; }
    if (kind == "interface") { return "external_interface"; }
    if (kind == "design") { return "design"; }
    if (kind == "unit_test" ) { return "unit_test_plan"; }
    if (kind == "system_test") { return "system_test_plan"; }
    if (kind == "e2e_test") { return "e2e_test_plan"; }
    if (kind == "milestone") { return "milestone"; }
    if (kind == "plan") { return "milestone"; }
    return nullptr;
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
    const char* from_kind;
    const char* type;
    const char* to_kind;
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

}

KiottySpecStatus Exchange::exportJsonl(Span<const char> directory)
{
    if (createDirectories(directory) != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _w.failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("디렉토리를 만들 수 없음: "), directory});
    }
    for (const OrderedTable& table : orderedTables()) {
        std::string sql = "SELECT * FROM ";
        sql += table.name;
        sql += " ORDER BY ";
        sql += table.key_order;
        auto query = _w.storage.prepare(spanOf(sql));
        if (!query) { return query.code; }
        std::string content;
        while (true) {
            const auto stepped = query.value.step();
            if (!stepped) { return _w.storage.check(stepped.code); }
            if (!stepped.value) { break; }
            content += dumpLine(rowObject(query.value.row(), table.name));
        }
        const auto path = tableFile(directory, table.name);
        if (!path) { return _w.failure.raise(path.code, {literalText("경로가 너무 김: "), directory}); }
        if (writeWholeFile(path.value.view(), spanOf(content)) != KIOTTY_SPEC_STATUS_SUCCESS) {
            return _w.failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("파일을 쓸 수 없음: "), path.value.view()});
        }
    }
    for (const std::string_view legacy : LEGACY_FILES) {
        const auto path = tableFile(directory, legacy);
        if (path) { removeFile(path.value.view()); }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<size_t, KiottySpecStatus> Exchange::importJsonl(Span<const char> directory, std::string& report)
{
    using CountResult = Result<size_t, KiottySpecStatus>;
    if (!isDirectory(directory)) { return CountResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("jsonl 디렉토리가 없음: "), directory})); }
    const auto legacyMarker = tableFile(directory, "link_version");
    const bool legacy = legacyMarker && isFile(legacyMarker.value.view());
    size_t notes = 0;
    KiottySpecStatus status = KIOTTY_SPEC_STATUS_SUCCESS;
    {
        Savepoint savepoint(_w.storage.connection());
        status = _w.storage.check(savepoint.begin());
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = recreate(_w.storage); }
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = legacy ? importLegacy(directory, report, notes) : importCurrent(directory); }
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

KiottySpecStatus Exchange::importCurrent(Span<const char> directory)
{
    std::vector<json> rows;
    for (const OrderedTable& table : orderedTables()) {
        bool present = false;
        if (!readObjects(directory, table.name, rows, present)) {
            return _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("jsonl 을 읽을 수 없음: "), spanOf(table.name)});
        }
        for (const json& row : rows) {
            std::string sql = "INSERT INTO ";
            sql += table.name;
            std::string columns;
            std::string values;
            int index = 0;
            for (const auto& entry : row.items()) {
                if (!isColumnName(entry.key())) {
                    return _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("알 수 없는 열: "), spanOf(entry.key())});
                }
                columns += (index == 0 ? "" : ", ") + entry.key();
                values += (index == 0 ? "?" : ", ?") + std::to_string(index + 1);
                ++index;
            }
            sql += " (" + columns + ") VALUES (" + values + ")";
            auto query = _w.storage.prepare(spanOf(sql));
            if (!query) { return query.code; }
            index = 1;
            for (const auto& entry : row.items()) { bindValue(query.value, index++, entry.value()); }
            const KiottySpecStatus inserted = _w.storage.check(query.value.execute());
            if (inserted != KIOTTY_SPEC_STATUS_SUCCESS) { return inserted; }
        }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus Exchange::importLegacy(Span<const char> directory, std::string& report, size_t& notes)
{
    std::vector<json> rows;
    bool present = false;
    auto readTable = [&](std::string_view table) -> KiottySpecStatus {
        if (readObjects(directory, table, rows, present)) { return KIOTTY_SPEC_STATUS_SUCCESS; }
        return _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("jsonl 을 읽을 수 없음: "), spanOf(table)});
    };
    auto run = [&](const char* sql, std::initializer_list<json> values) -> KiottySpecStatus {
        auto query = _w.storage.prepare(boundedText(sql, 512));
        if (!query) { return query.code; }
        int index = 1;
        for (const json& value : values) { bindValue(query.value, index++, value); }
        return _w.storage.check(query.value.execute());
    };

    KiottySpecStatus status = readTable("meta");
    for (const json& row : rows) {
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { break; }
        status = run("INSERT INTO meta (key, value) VALUES (?1, ?2)", {textOf(row, "key"), textOf(row, "value")});
    }
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }

    status = readTable("id_counter");
    for (const json& row : rows) {
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { break; }
        const std::string prefix = textOf(row, "prefix");
        if (prefix.empty() || prefix[0] == '#' || prefix == "FILE") { continue; }
        status = run("INSERT INTO id_counter (prefix, next) VALUES (?1, ?2)", {prefix, integerOf(row, "next", 1)});
    }
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }

    // 체크포인트: confirmed → DONE, 나머지 → RUNNING. note 는 63 바이트로 자른다.
    status = readTable("checkpoint");
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    std::sort(rows.begin(), rows.end(), [](const json& left, const json& right) { return integerOf(left, "seq", 0) < integerOf(right, "seq", 0); });
    int64_t previous = 0;
    for (const json& row : rows) {
        const int64_t sequence = integerOf(row, "seq", 0);
        const bool release = textOf(row, "purpose") == "release";
        const std::string description = textOf(row, "note");
        const std::string cut = cutUtf8(description, entity::MAX_DESCRIPTION_LENGTH);
        if (cut.size() < description.size()) { note(report, notes, checkpointLabel(sequence) + " 설명을 63 바이트로 자름: " + description); }
        status = run("INSERT INTO checkpoint (sequence, created_at, previous, step, state, description) VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
                     {sequence, integerOf(row, "created_at", 0), previous, mapLegacyStep(integerOf(row, "step", 1), release),
                      integerOf(row, "confirmed", 0) != 0 ? "done" : "running", cut});
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
        previous = sequence;
    }
    const int64_t latest = previous;

    // 항목: category → 태그, 제목이 127 바이트를 넘으면 자르고 원래 제목을 요약에 둔다.
    status = readTable("item_version");
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    std::map<std::string, std::string> kinds;
    size_t pending = 0;
    for (const json& row : rows) {
        const std::string id = textOf(row, "item_id");
        const char* kind = mapLegacyKind(textOf(row, "kind"));
        if (kind == nullptr) {
            note(report, notes, id + " 알 수 없는 Kind 라 건너뜀");
            continue;
        }
        kinds[id] = kind;
        const ItemKind item_kind = static_cast<ItemKind>(entity::parseKind(kind));
        int64_t checkpoint = integerOf(row, "cp_seq", latest);
        if (checkpoint == LEGACY_PENDING) {
            checkpoint = latest;
            ++pending;
        }
        json tags = json::array();
        const std::string category = textOf(row, "category");
        for (const LegacyCategory& entry : LEGACY_CATEGORIES) {
            if (category != entry.category) { continue; }
            const auto tag = _w.catalog.finder().findTag(item_kind, boundedText(entry.tag, 64));
            if (!tag) { break; }
            const auto bit = _w.catalog.finder().findTagBit(item_kind, tag.value, boundedText(entry.bit, 64));
            if (bit) { tags.push_back(json::array({tag.value, uint64_t{1} << bit.value})); }
            break;
        }
        const std::string title = textOf(row, "title");
        std::string summary;
        std::string cut = cutUtf8(title, entity::MAX_TITLE_LENGTH);
        if (cut.size() < title.size()) {
            summary = cutUtf8(title, entity::MAX_SUMMARY_LENGTH);
            note(report, notes, id + " 제목을 127 바이트로 자르고 원래 제목을 요약에 둠");
        }
        std::string body = textOf(row, "body");
        if (body.size() > entity::MAX_BODY_LENGTH) {
            body = cutUtf8(body, entity::MAX_BODY_LENGTH);
            note(report, notes, id + " 본문을 16384 바이트로 자름");
        }
        json parent = hasText(row, "parent_id") ? json(textOf(row, "parent_id")) : json(nullptr);
        status = run("INSERT OR REPLACE INTO item_version (item_id, checkpoint, kind, parent_id, title, summary, body, tags, removed) "
                     "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)",
                     {id, checkpoint, kind, parent, cut, summary, body, tags.dump(), integerOf(row, "removed", 0)});
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    }
    if (pending > 0) { note(report, notes, "체크포인트에 묶이지 않은 쓰기 " + std::to_string(pending) + " 건을 가장 최근 체크포인트에 묶음"); }

    // 링크 → 관계. 파일 링크는 파일 엔티티와 함께 없어진다 (7).
    status = readTable("link_version");
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    size_t file_links = 0;
    for (const json& row : rows) {
        const std::string from = textOf(row, "from_id");
        const std::string to = textOf(row, "to_id");
        const std::string type = textOf(row, "type");
        const auto from_kind = kinds.find(from);
        const auto to_kind = kinds.find(to);
        if (from_kind == kinds.end() || to_kind == kinds.end()) {
            ++file_links;
            continue;
        }
        const char* relation = nullptr;
        for (const LegacyLink& link : LEGACY_LINKS) {
            if (from_kind->second == link.from_kind && type == link.type && to_kind->second == link.to_kind) { relation = link.relation; }
        }
        const ItemKind model = static_cast<ItemKind>(entity::parseKind(from_kind->second));
        const auto index = relation == nullptr ? Result<uint32_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND)
                                               : _w.catalog.finder().findRelation(model, boundedText(relation, 64));
        if (!index) {
            note(report, notes, from + " -" + type + "-> " + to + " 링크는 관계 정의에 없어 건너뜀");
            continue;
        }
        int64_t checkpoint = integerOf(row, "cp_seq", latest);
        if (checkpoint == LEGACY_PENDING) { checkpoint = latest; }
        status = run("INSERT OR REPLACE INTO relation_version (from_id, model, relation, to_id, checkpoint, removed) VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
                     {from, from_kind->second, index.value, to, checkpoint, integerOf(row, "removed", 0)});
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    }
    if (file_links > 0) { note(report, notes, "파일 엔티티와 이어진 링크 " + std::to_string(file_links) + " 건을 지움 (설계서 v1 7)"); }

    status = readTable("decision_version");
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    for (const json& row : rows) {
        const std::string legacy_kind = textOf(row, "kind");
        const char* kind = mapLegacyKind(legacy_kind);
        if (kind == nullptr) {
            note(report, notes, "결정 기록 " + legacy_kind + " 는 Kind 를 알 수 없어 건너뜀");
            continue;
        }
        const int64_t checkpoint = integerOf(row, "cp_seq", latest) == LEGACY_PENDING ? latest : integerOf(row, "cp_seq", latest);
        std::string text = textOf(row, "body");
        if (text.size() > MAX_DECISION_BYTES) {
            note(report, notes, std::string("결정 기록 ") + kind + " (" + checkpointLabel(checkpoint) + ") 을 2048 바이트로 자름 (원래 " +
                                    std::to_string(text.size()) + " 바이트)");
            text = cutUtf8(text, MAX_DECISION_BYTES);
        }
        status = run("INSERT OR REPLACE INTO decision_version (kind, checkpoint, description) VALUES (?1, ?2, ?3)", {kind, checkpoint, text});
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    }

    const std::string_view skipped[] = {"file_version", "change_request", "result", "milestone_state"};
    for (const std::string_view table : skipped) {
        status = readTable(table);
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
        if (!rows.empty()) { note(report, notes, std::string(table) + " 의 " + std::to_string(rows.size()) + " 줄은 옮기지 않음"); }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

}
