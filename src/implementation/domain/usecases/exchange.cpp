#include "implementation/domain/usecases/exchange.h"

#include "implementation/core/markdown.h"
#include "implementation/core/text.h"
#include "implementation/core/text_view.h"
#include "implementation/data/datasource/file_system.h"
#include "implementation/data/datasource/savepoint.h"
#include "implementation/data/datasource/schema.h"

#include <nlohmann/json.hpp>

namespace kiotty::spec::implementation {

using nlohmann::json;

namespace {

constexpr char JSONL_EXTENSION[] = ".jsonl";
constexpr char META_TABLE[] = "meta";
constexpr char COUNTER_TABLE[] = "id_counter";
constexpr char RAISE_COUNTER_SQL[] = "UPDATE id_counter SET next = MAX(next, ?2) WHERE prefix = ?1";

constexpr std::pair<KiottySpecItemKind, KiottySpecItemKind> TRACE_PAIRS[] = {
    {KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, KIOTTY_SPEC_ITEM_KIND_INTERFACE},
    {KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, KIOTTY_SPEC_ITEM_KIND_DESIGN},
    {KIOTTY_SPEC_ITEM_KIND_INTERFACE, KIOTTY_SPEC_ITEM_KIND_DESIGN},
    {KIOTTY_SPEC_ITEM_KIND_DESIGN, KIOTTY_SPEC_ITEM_KIND_UNIT_TEST},
    {KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST},
    {KIOTTY_SPEC_ITEM_KIND_INTERFACE, KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST},
    {KIOTTY_SPEC_ITEM_KIND_MILESTONE, KIOTTY_SPEC_ITEM_KIND_DESIGN},
};

nlohmann::ordered_json orderedRow(const Row &row)
{
    nlohmann::ordered_json object = nlohmann::ordered_json::object();
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
    return object;
}

void bindJson(Query &query, int index, const json &value)
{
    if (value.is_null()) {
        query.bind(index, nullptr);
        return;
    }
    if (value.is_boolean()) {
        query.bind(index, static_cast<int64_t>(value.get<bool>() ? 1 : 0));
        return;
    }
    if (value.is_number_integer()) {
        query.bind(index, value.get<int64_t>());
        return;
    }
    if (value.is_number()) {
        query.bind(index, value.get<double>());
        return;
    }
    const std::string text = value.is_string() ? value.get<std::string>() : value.dump();
    query.bind(index, spanOf(text));
}

void bindAll(Query &query, const json &row)
{
    int index = 1;
    for (const auto &entry : row.items()) { bindJson(query, index++, entry.value()); }
}

std::string insertSql(const std::string &table, const json &row, bool merge)
{
    std::string columns;
    std::string placeholders;
    int index = 0;
    for (const auto &entry : row.items()) {
        columns += (index == 0 ? "" : ", ") + entry.key();
        placeholders += (index == 0 ? "?" : ", ?") + std::to_string(index + 1);
        ++index;
    }
    const std::string verb = merge ? "INSERT OR IGNORE" : "INSERT";
    return verb + " INTO " + table + " (" + columns + ") VALUES (" + placeholders + ")";
}

std::string sameRowSql(const std::string &table, const json &row)
{
    std::string where;
    int column = 1;
    for (const auto &entry : row.items()) {
        where += (column == 1 ? "" : " AND ") + entry.key() + " IS ?" + std::to_string(column);
        ++column;
    }
    return "SELECT COUNT(*) FROM " + table + " WHERE " + where;
}

std::string textField(const json &row, const char *key)
{
    const auto found = row.find(key);
    if (found == row.end() || !found->is_string()) { return std::string(); }
    return found->get<std::string>();
}

int64_t integerField(const json &row, const char *key)
{
    const auto found = row.find(key);
    if (found == row.end() || !found->is_number_integer()) { return 0; }
    return found->get<int64_t>();
}

KiottySpecStatus raiseCounter(Storage &storage, const json &row)
{
    auto query = storage.prepare(literalText(RAISE_COUNTER_SQL));
    if (!query) { return query.code; }
    const std::string prefix = textField(row, "prefix");
    query.value.bind(1, spanOf(prefix));
    query.value.bind(2, integerField(row, "next"));
    return storage.check(query.value.run());
}

FlagResult sameRowExists(Storage &storage, const std::string &table, const json &row)
{
    auto query = storage.prepare(spanOf(sameRowSql(table, row)));
    if (!query) { return FlagResult::failure(query.code); }
    bindAll(query.value, row);
    const auto stepped = query.value.step();
    if (!stepped) { return FlagResult::failure(storage.check(stepped.code)); }
    return FlagResult::success(query.value.row().integer(0) != 0);
}

KiottySpecItemCategory splitCategory(std::string &heading)
{
    KiottySpecItemCategory category = KIOTTY_SPEC_ITEM_CATEGORY_NONE;
    const std::string_view trimmed = trim(heading);
    if (trimmed.empty() || trimmed.front() != '[') { return category; }
    const size_t close = trimmed.find(']');
    if (close == std::string_view::npos) { return category; }
    const std::string tag(trim(trimmed.substr(1, close - 1)));
    parse(tag, category);
    heading = std::string(trim(trimmed.substr(close + 1)));
    return category;
}

KiottySpecItemCategory defaultCategory(KiottySpecItemKind kind)
{
    switch (kind) {
    case KIOTTY_SPEC_ITEM_KIND_REQUIREMENT: return KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_FUNCTIONAL;
    case KIOTTY_SPEC_ITEM_KIND_RULE: return KIOTTY_SPEC_ITEM_CATEGORY_RULE_ARCHITECTURE;
    case KIOTTY_SPEC_ITEM_KIND_INTERFACE: return KIOTTY_SPEC_ITEM_CATEGORY_INTERFACE_C_API;
    case KIOTTY_SPEC_ITEM_KIND_DESIGN: return KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_MODULE;
    case KIOTTY_SPEC_ITEM_KIND_UNIT_TEST:
    case KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST:
    case KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST:
    case KIOTTY_SPEC_ITEM_KIND_MILESTONE: return KIOTTY_SPEC_ITEM_CATEGORY_NONE;
    }
    return KIOTTY_SPEC_ITEM_CATEGORY_NONE;
}

std::string sectionBody(std::string_view body)
{
    std::string out;
    for (const auto line : splitLines(body)) {
        std::string text(line);
        if (text.rfind("###", 0) == 0) { text.erase(0, 1); }
        out += text + "\n";
    }
    return out;
}

}

TextResult Exchange::traceReport()
{
    std::string out = "# 추적 보고서\n\n";
    for (const auto &[row, col] : TRACE_PAIRS) {
        const auto matrix = _graph.matrix(row, col);
        if (!matrix) { return matrix; }
        out += "## " + std::string(label(row)) + " × " + std::string(label(col)) + "\n```csv\n" + matrix.value + "```\n\n";
    }
    const auto checks = _checker.all();
    if (!checks) { return checks; }
    return TextResult::success(out + checks.value);
}

KiottySpecStatus Exchange::exportJsonl(Span<const char> directory)
{
    if (createDirectories(directory) != KIOTTY_SPEC_STATUS_SUCCESS) { return _failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("디렉토리를 만들 수 없음: "), directory}); }
    for (const OrderedTable &ordered : orderedTables()) {
        const KiottySpecStatus exported = exportTable(directory, ordered.name, ordered.key_order);
        if (exported != KIOTTY_SPEC_STATUS_SUCCESS) { return exported; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

ImportReportResult Exchange::importJsonl(Span<const char> directory, bool merge)
{
    ImportReport report;
    Savepoint savepoint(_storage.connection());
    const KiottySpecStatus begun = _storage.check(savepoint.begin());
    if (begun != KIOTTY_SPEC_STATUS_SUCCESS) { return ImportReportResult::failure(begun); }
    for (const OrderedTable &ordered : orderedTables()) {
        const std::string file = std::string(ordered.name) + JSONL_EXTENSION;
        const auto path = joinPath(directory, spanOf(file));
        if (!path) { return ImportReportResult::failure(_failure.raise(path.code, {literalText("경로가 너무 김: "), directory})); }
        const auto content = readWholeFile(path.value.view());
        if (!content) { continue; }
        const KiottySpecStatus imported = importTable(ordered.name, *content, merge, report);
        if (imported != KIOTTY_SPEC_STATUS_SUCCESS) { return ImportReportResult::failure(imported); }
    }
    const KiottySpecStatus committed = _storage.check(savepoint.commit());
    if (committed != KIOTTY_SPEC_STATUS_SUCCESS) { return ImportReportResult::failure(committed); }
    return ImportReportResult::success(std::move(report));
}

TextListResult Exchange::importMarkdown(KiottySpecItemKind kind, std::string_view markdown)
{
    std::vector<std::string> created;
    Savepoint savepoint(_storage.connection());
    const KiottySpecStatus begun = _storage.check(savepoint.begin());
    if (begun != KIOTTY_SPEC_STATUS_SUCCESS) { return TextListResult::failure(begun); }
    for (auto &section : splitSections(markdown, 2)) {
        std::string title = section.heading;
        KiottySpecItemCategory category = splitCategory(title);
        if (category == KIOTTY_SPEC_ITEM_CATEGORY_NONE) { category = defaultCategory(kind); }
        auto id = _items.create(kind, category, std::nullopt, title, sectionBody(section.body));
        if (!id) { return TextListResult::failure(id.code); }
        created.push_back(std::move(id.value));
    }
    if (created.empty()) { return TextListResult::failure(_failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("가져올 '## 제목' 절이 없음")})); }
    const KiottySpecStatus committed = _storage.check(savepoint.commit());
    if (committed != KIOTTY_SPEC_STATUS_SUCCESS) { return TextListResult::failure(committed); }
    return TextListResult::success(std::move(created));
}

TextResult Exchange::queryReadonly(std::string_view sql)
{
    auto query = _storage.prepare(spanOf(sql));
    if (!query) { return TextResult::failure(query.code); }
    if (!query.value.isReadOnly()) { return TextResult::failure(_failure.raise(KIOTTY_SPEC_STATUS_PERMISSION, {literalText("읽기 전용 SQL 만 허용됨")})); }
    nlohmann::ordered_json rows = nlohmann::ordered_json::array();
    auto stepped = query.value.step();
    while (stepped.value) {
        rows.push_back(orderedRow(query.value.row()));
        stepped = query.value.step();
    }
    if (!stepped) { return TextResult::failure(_storage.check(stepped.code)); }
    return TextResult::success(rows.dump(2));
}

KiottySpecStatus Exchange::exportTable(Span<const char> directory, std::string_view table, std::string_view keyOrder)
{
    const std::string sql = "SELECT * FROM " + std::string(table) + " ORDER BY " + std::string(keyOrder);
    auto query = _storage.prepare(spanOf(sql));
    if (!query) { return query.code; }
    std::string content;
    auto stepped = query.value.step();
    while (stepped.value) {
        content += json(orderedRow(query.value.row())).dump() + "\n";
        stepped = query.value.step();
    }
    if (!stepped) { return _storage.check(stepped.code); }
    const std::string file = std::string(table) + JSONL_EXTENSION;
    const auto path = joinPath(directory, spanOf(file));
    if (!path) { return _failure.raise(path.code, {literalText("경로가 너무 김: "), directory}); }
    if (writeWholeFile(path.value.view(), spanOf(content)) == KIOTTY_SPEC_STATUS_SUCCESS) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _failure.raise(KIOTTY_SPEC_STATUS_INPUT_OUTPUT, {literalText("파일을 쓸 수 없음: "), path.value.view()});
}

KiottySpecStatus Exchange::importTable(std::string_view table, std::string_view content, bool merge, ImportReport &report)
{
    const std::string name(table);
    if (!merge) {
        const KiottySpecStatus cleared = _storage.execute(spanOf("DELETE FROM " + name));
        if (cleared != KIOTTY_SPEC_STATUS_SUCCESS) { return cleared; }
    }
    size_t lineNo = 0;
    for (const auto line : splitLines(content)) {
        ++lineNo;
        if (trim(line).empty()) { continue; }
        const KiottySpecStatus imported = importRow(name, line, lineNo, merge, report);
        if (imported != KIOTTY_SPEC_STATUS_SUCCESS) { return imported; }
        ++report.rows;
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus Exchange::importRow(const std::string &table, std::string_view line, size_t lineNo, bool merge, ImportReport &report)
{
    const json row = json::parse(line.begin(), line.end(), nullptr, false);
    if (row.is_discarded() || !row.is_object()) {
        const std::string where = table + ".jsonl " + std::to_string(lineNo) + "행 파싱 실패: 올바른 JSON 객체가 아님";
        return _failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {spanOf(where)});
    }
    auto query = _storage.prepare(spanOf(insertSql(table, row, merge)));
    if (!query) { return query.code; }
    bindAll(query.value, row);
    const KiottySpecStatus ran = _storage.check(query.value.run());
    if (ran != KIOTTY_SPEC_STATUS_SUCCESS) { return ran; }
    const bool skipped = merge && _storage.changedRowCount() == 0 && table != META_TABLE;
    if (!skipped) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    if (table == COUNTER_TABLE) { return raiseCounter(_storage, row); }
    const auto same = sameRowExists(_storage, table, row);
    if (!same) { return same.code; }
    if (!same.value) { report.conflicts.push_back(table + ": " + row.dump()); }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

}
