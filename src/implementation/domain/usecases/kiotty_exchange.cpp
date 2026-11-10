#include "implementation/domain/usecases/kiotty_exchange.h"

#include "implementation/core/kiotty_decimal.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_json_writer.h"
#include "implementation/core/kiotty_span_text.h"
#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/datasource/kiotty_file_system.h"
#include "implementation/data/datasource/kiotty_file_writer.h"
#include "implementation/data/datasource/kiotty_jsonl_file.h"
#include "implementation/data/datasource/kiotty_savepoint.h"
#include "implementation/domain/usecases/kiotty_legacy_importer.h"

#include <cstdint>
#include <string_view>

namespace kiotty::spec::implementation {

namespace {

// 정의서 v0 에만 있던 파일. 내보낼 때 지운다. link_version 이 있으면 옛 형식으로 본다.
constexpr std::string_view LEGACY_FILES[] = {"link_version", "file_version", "milestone_state", "change_request", "result"};
constexpr std::string_view LEGACY_MARKER = "link_version";
// 스키마 3 부터 있는 파일. 없으면 버전 행이 ID 로 항목을 가리키는 옛 형식이다.
constexpr std::string_view ITEM_KEY_MARKER = "item";
constexpr size_t MAX_COLUMNS = 32;
// 작업 버퍼에서 ID · Kind 같은 짧은 값에 주는 칸
constexpr size_t SHORT_AREA_BYTES = 1024;
constexpr size_t SHORT_AREA_SHARE = 4;
// 스택 버퍼는 1024 바이트 이하 (개발수칙 1.7)
constexpr size_t MAX_SQL_LENGTH = 512;
constexpr size_t MAX_VALUES_LENGTH = 192;

using SqlText = FixedString<MAX_SQL_LENGTH>;

struct InsertSql {
    SqlText sql;
    // 열 이름이 아닌 키. 있으면 status 는 INVALID_ARGUMENT
    Span<const char> unknown;
    KiottySpecStatus status {KIOTTY_SPEC_STATUS_SUCCESS};
};

struct ColumnOrder {
    int columns[MAX_COLUMNS] {};
    int count {0};
};

bool isColumnName(Span<const char> name)
{
    if (name.length() == 0) { return false; }
    for (const char letter : name) {
        if (!((letter >= 'a' && letter <= 'z') || letter == '_')) { return false; }
    }
    return true;
}

// 열 이름 순서 (예전 nlohmann::json 의 키 순서와 같다)
ColumnOrder orderColumns(const Row& row)
{
    ColumnOrder order;
    order.count = row.count();
    for (int column = 0; column < order.count; ++column) {
        int position = column;
        while (position > 0 && viewOf(row.name(order.columns[position - 1])) > viewOf(row.name(column))) {
            order.columns[position] = order.columns[position - 1];
            --position;
        }
        order.columns[position] = column;
    }
    return order;
}

// 태그는 DB 에 "[[1,4]]" 글자로 두고, jsonl 에는 배열로 쓴다 (3.6). 배열 모양이 아니면 [].
KiottySpecStatus appendTags(TextBuffer& line, Span<const char> tags)
{
    bool valid = tags.length() >= 2 && tags[0] == '[' && tags[tags.length() - 1] == ']';
    for (const char letter : tags) {
        const bool digit = letter >= '0' && letter <= '9';
        valid = valid && (digit || letter == '[' || letter == ']' || letter == ',' || letter == '-' || letter == ' ');
    }
    if (!valid) { return line.append(literalText("[]")); }
    KiottySpecStatus status = KIOTTY_SPEC_STATUS_SUCCESS;
    for (size_t index = 0; index < tags.length() && status == KIOTTY_SPEC_STATUS_SUCCESS; ++index) {
        if (tags[index] != ' ') { status = line.append(Span<const char>(&tags[index], 1)); }
    }
    return status;
}

KiottySpecStatus appendValue(TextBuffer& line, const Row& row, int column)
{
    switch (row.type(column)) {
    case ColumnType::INTEGER: return appendJsonInteger(line, row.integer(column));
    case ColumnType::FLOAT: return appendJsonReal(line, row.real(column));
    case ColumnType::NULL_VALUE: return line.append(literalText("null"));
    case ColumnType::TEXT:
    case ColumnType::BLOB: break;
    }
    // tags 열만 배열로 쓴다. tags 는 item_version 에만 있다.
    if (viewOf(row.name(column)) == "tags") { return appendTags(line, row.text(column)); }
    return appendJsonString(line, row.text(column));
}

KiottySpecStatus appendRow(TextBuffer& line, const Row& row, const ColumnOrder& order)
{
    KiottySpecStatus status = line.append(literalText("{"));
    for (int position = 0; position < order.count && status == KIOTTY_SPEC_STATUS_SUCCESS; ++position) {
        const int column = order.columns[position];
        if (position > 0) { status = line.append(literalText(",")); }
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = appendJsonString(line, row.name(column)); }
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = line.append(literalText(":")); }
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = appendValue(line, row, column); }
    }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = line.append(literalText("}\n")); }
    return status;
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

// "INSERT INTO table (a, b) VALUES (?1, ?2)"
InsertSql buildInsert(const OrderedTable& table, JsonReader& row, Span<char> scratch)
{
    InsertSql insert;
    SqlText& sql = insert.sql;
    FixedString<MAX_VALUES_LENGTH> values;
    sql.assign(literalText("INSERT INTO "));
    sql.append(spanOf(table.name));
    sql.append(literalText(" ("));
    JsonField field;
    Result<bool, KiottySpecStatus> next = row.next(field, scratch);
    uint64_t index = 0;
    KiottySpecStatus built = KIOTTY_SPEC_STATUS_SUCCESS;
    while (next && next.value && built == KIOTTY_SPEC_STATUS_SUCCESS) {
        if (!isColumnName(field.key)) {
            insert.unknown = field.key;
            insert.status = KIOTTY_SPEC_STATUS_INVALID_ARGUMENT;
            return insert;
        }
        ++index;
        built = sql.append(index == 1 ? literalText("") : literalText(", "));
        if (built == KIOTTY_SPEC_STATUS_SUCCESS) { built = sql.append(field.key); }
        if (built == KIOTTY_SPEC_STATUS_SUCCESS) { built = values.append(index == 1 ? literalText("?") : literalText(", ?")); }
        if (built == KIOTTY_SPEC_STATUS_SUCCESS) { built = appendDecimal(values, index); }
        next = row.next(field, scratch);
    }
    if (built == KIOTTY_SPEC_STATUS_SUCCESS) { built = sql.append(literalText(") VALUES (")); }
    if (built == KIOTTY_SPEC_STATUS_SUCCESS) { built = sql.append(values.view()); }
    if (built == KIOTTY_SPEC_STATUS_SUCCESS) { built = sql.append(literalText(")")); }
    insert.status = next ? built : next.code;
    return insert;
}

}

ImportAreas Exchange::areas()
{
    const Span<char> all = _work.scratch();
    const size_t half = all.length() / 2;
    const size_t rest = all.length() - half;
    const size_t small = rest / SHORT_AREA_SHARE < SHORT_AREA_BYTES ? rest / SHORT_AREA_SHARE : SHORT_AREA_BYTES;
    ImportAreas areas;
    areas.line = Span<char>(all.data(), half);
    areas.first = Span<char>(all.data() + half, small);
    areas.second = Span<char>(all.data() + half + small, small);
    areas.text = Span<char>(all.data() + half + small * 2, rest - small * 2);
    return areas;
}

KiottySpecStatus Exchange::exportJsonl(Span<const char> directory)
{
    if (createDirectories(directory) != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _w.failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("디렉토리를 만들 수 없음: "), directory});
    }
    for (const OrderedTable& table : orderedTables()) {
        const KiottySpecStatus status = exportTable(directory, table);
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    }
    for (const std::string_view legacy : LEGACY_FILES) {
        const SystemPathResult path = jsonlPath(directory, legacy);
        if (path) { removeFile(path.value.view()); }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus Exchange::exportTable(Span<const char> directory, const OrderedTable& table)
{
    SqlText sql;
    sql.assign(literalText("SELECT * FROM "));
    sql.append(spanOf(table.name));
    sql.append(literalText(" ORDER BY "));
    if (sql.append(spanOf(table.key_order)) != KIOTTY_SPEC_STATUS_SUCCESS) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    auto query = _w.storage.prepare(sql.view());
    if (!query) { return query.code; }
    if (static_cast<size_t>(query.value.row().count()) > MAX_COLUMNS) {
        return _w.failure.raise(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, {literalText("열이 너무 많음: "), spanOf(table.name)});
    }
    const ColumnOrder order = orderColumns(query.value.row());
    const SystemPathResult path = jsonlPath(directory, table.name);
    FileWriter file;
    if (!path || file.open(path.value.view()) != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _w.failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("jsonl 을 쓸 수 없음: "), spanOf(table.name)});
    }
    Result<bool, KiottySpecStatus> stepped = query.value.step();
    while (stepped && stepped.value) {
        _work.clear();
        if (appendRow(_work, query.value.row(), order) != KIOTTY_SPEC_STATUS_SUCCESS) {
            return _w.failure.raise(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, {literalText("한 줄이 작업 버퍼보다 큼: "), spanOf(table.name)});
        }
        if (file.write(_work.view()) != KIOTTY_SPEC_STATUS_SUCCESS) {
            return _w.failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("jsonl 을 쓸 수 없음: "), spanOf(table.name)});
        }
        stepped = query.value.step();
    }
    return stepped ? KIOTTY_SPEC_STATUS_SUCCESS : _w.storage.check(stepped.code);
}

Result<size_t, KiottySpecStatus> Exchange::importJsonl(Span<const char> directory, Span<char> report)
{
    using CountResult = Result<size_t, KiottySpecStatus>;
    if (!isDirectory(directory)) { return CountResult::failure(_w.failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("jsonl 디렉토리가 없음: "), directory})); }
    KiottySpecStatus status = KIOTTY_SPEC_STATUS_SUCCESS;
    size_t notes = 0;
    {
        Savepoint savepoint(_w.storage.connection());
        status = _w.storage.check(savepoint.begin());
        const SystemPathResult marker = jsonlPath(directory, ITEM_KEY_MARKER);
        const bool keyed = marker && isFile(marker.value.view());
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = keyed ? recreate(_w.storage) : recreateWithIdKeys(_w.storage); }
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
            const CountResult imported = importAll(directory, report);
            status = imported.code;
            notes = imported ? imported.value : 0;
        }
        if (status == KIOTTY_SPEC_STATUS_SUCCESS && !keyed) { status = migrateItemKeys(_w.storage); }
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = _w.storage.check(savepoint.commit()); }
    }
    const KiottySpecStatus reloaded = _w.reload();
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(status); }
    if (reloaded != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(reloaded); }
    return CountResult::success(notes);
}

Result<size_t, KiottySpecStatus> Exchange::importAll(Span<const char> directory, Span<char> report)
{
    using CountResult = Result<size_t, KiottySpecStatus>;
    const ImportAreas work = areas();
    const SystemPathResult marker = jsonlPath(directory, LEGACY_MARKER);
    if (marker && isFile(marker.value.view())) {
        SpanText text(report);
        return LegacyImporter(_w, work, text).run(directory);
    }
    const KiottySpecStatus status = importCurrent(directory, work);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return CountResult::failure(status); }
    if (report.length() > 0) { report[0] = '\0'; }
    return CountResult::success(0);
}

KiottySpecStatus Exchange::importCurrent(Span<const char> directory, const ImportAreas& areas)
{
    const SystemPathResult marker = jsonlPath(directory, ITEM_KEY_MARKER);
    const bool keyed = marker && isFile(marker.value.view());
    for (const OrderedTable& table : orderedTables()) {
        if (!keyed && table.name == ITEM_KEY_MARKER) { continue; }
        const KiottySpecStatus status = importTable(directory, table, areas);
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus Exchange::importTable(Span<const char> directory, const OrderedTable& table, const ImportAreas& areas)
{
    JsonlFile file(areas.line, areas.text);
    if (file.open(directory, table.name) != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _w.failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("jsonl 을 열 수 없음: "), spanOf(table.name)});
    }
    Result<bool, KiottySpecStatus> next = file.next();
    while (next && next.value) {
        const KiottySpecStatus status = insertRow(table, file.row(), areas.text);
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
        next = file.next();
    }
    if (!next) { return _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("jsonl 을 읽을 수 없음: "), spanOf(table.name)}); }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus Exchange::insertRow(const OrderedTable& table, JsonReader& row, Span<char> scratch)
{
    const InsertSql insert = buildInsert(table, row, scratch);
    if (insert.unknown.length() > 0) { return _w.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("알 수 없는 열: "), insert.unknown}); }
    if (insert.status != KIOTTY_SPEC_STATUS_SUCCESS) { return _w.failure.raise(insert.status, {literalText("열이 너무 많음: "), spanOf(table.name)}); }
    auto query = _w.storage.prepare(insert.sql.view());
    if (!query) { return query.code; }
    row.rewind();
    JsonField field;
    Result<bool, KiottySpecStatus> next = row.next(field, scratch);
    for (int position = 1; next && next.value; ++position) {
        bindField(query.value, position, field);
        next = row.next(field, scratch);
    }
    return _w.storage.check(query.value.execute());
}

}
