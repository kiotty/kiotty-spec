#include "implementation/domain/usecases/exchange.h"

#include "implementation/data/datasource/schema.h"
#include "implementation/core/error.h"
#include "implementation/core/markdown.h"
#include "implementation/core/text.h"

#include <nlohmann/json.hpp>

#include <map>

namespace kiotty::spec::implementation {

using nlohmann::json;

namespace {

nlohmann::ordered_json orderedRow(const Statement &stmt) {
    nlohmann::ordered_json row = nlohmann::ordered_json::object();
    for (int i = 0; i < stmt.columnCount(); ++i) {
        const std::string column = stmt.columnName(i);
        switch (stmt.columnType(i)) {
        case ColumnType::INTEGER: row[column] = stmt.integer(i); break;
        case ColumnType::FLOAT: row[column] = stmt.real(i); break;
        case ColumnType::NULL_VALUE: row[column] = nullptr; break;
        default: row[column] = stmt.text(i); break;
        }
    }
    return row;
}

json rowToJson(const Statement &stmt) { return json(orderedRow(stmt)); }

void bindJson(Statement &stmt, int index, const json &value) {
    if (value.is_null()) stmt.bindOne(index, nullptr);
    else if (value.is_number_integer()) stmt.bindOne(index, value.get<int64_t>());
    else if (value.is_number()) stmt.bindOne(index, value.get<double>());
    else if (value.is_boolean()) stmt.bindOne(index, value.get<bool>());
    else if (value.is_string()) stmt.bindOne(index, value.get<std::string>());
    else stmt.bindOne(index, value.dump());
}

KiottySpecItemCategory splitCategory(std::string &heading) {
    KiottySpecItemCategory category = KIOTTY_SPEC_ITEM_CATEGORY_NONE;
    const std::string_view trimmed = trim(heading);
    if (!trimmed.empty() && trimmed.front() == '[') {
        const size_t close = trimmed.find(']');
        if (close != std::string_view::npos) {
            const std::string tag(trim(trimmed.substr(1, close - 1)));
            parse(tag, category);
            heading = std::string(trim(trimmed.substr(close + 1)));
        }
    }
    return category;
}

}

std::string Exchange::exportMarkdown(KiottySpecItemKind kind) {
    std::string out = "# " + std::string(label(kind)) + "\n\n";
    const auto items = expect(items_.ofKind(kind), failure_);
    if (items.empty()) return out + "없음\n";
    for (const auto &item : items) {
        out += context_.renderItem(item, true);
    }
    return out;
}

std::string Exchange::traceReport() {
    std::string out = "# 추적 보고서\n\n";
    const std::vector<std::pair<KiottySpecItemKind, KiottySpecItemKind>> pairs{
        {KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, KIOTTY_SPEC_ITEM_KIND_INTERFACE},
        {KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, KIOTTY_SPEC_ITEM_KIND_DESIGN},
        {KIOTTY_SPEC_ITEM_KIND_INTERFACE, KIOTTY_SPEC_ITEM_KIND_DESIGN},
        {KIOTTY_SPEC_ITEM_KIND_DESIGN, KIOTTY_SPEC_ITEM_KIND_UNIT_TEST},
        {KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST},
        {KIOTTY_SPEC_ITEM_KIND_INTERFACE, KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST},
        {KIOTTY_SPEC_ITEM_KIND_MILESTONE, KIOTTY_SPEC_ITEM_KIND_DESIGN},
    };
    for (const auto &[row, col] : pairs) {
        out += "## " + std::string(label(row)) + " × " + std::string(label(col)) + "\n```csv\n" +
               expect(links_.matrix(row, col), failure_) + "```\n\n";
    }
    return out + checker_.all();
}

void Exchange::exportJsonl(const std::filesystem::path &dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    require(!ec, KIOTTY_SPEC_STATUS_INPUT_OUTPUT, "디렉토리를 만들 수 없음: " + pathToUtf8(dir));
    for (const OrderedTable &ordered : orderedTables()) {
        const std::string_view table = ordered.name;
        auto stmt = db_.prepare("SELECT * FROM " + std::string(table) + " ORDER BY " + std::string(ordered.key_order));
        std::string content;
        while (stmt.step()) {
            content += rowToJson(stmt).dump() + "\n";
        }
        writeFile(dir / (std::string(table) + ".jsonl"), content);
    }
}

ImportReport Exchange::importJsonl(const std::filesystem::path &dir, bool merge) {
    ImportReport report;
    LegacySavepoint savepoint(db_);
    for (const OrderedTable &ordered : orderedTables()) {
        const std::string_view table = ordered.name;
        const auto content = readFile(dir / (std::string(table) + ".jsonl"));
        if (!content) continue;
        const std::string name(table);
        if (!merge) db_.exec("DELETE FROM " + name);
        size_t lineNo = 0;
        for (const auto line : splitLines(*content)) {
            ++lineNo;
            if (trim(line).empty()) continue;
            json row;
            try {
                row = json::parse(line);
            } catch (const std::exception &e) {
                fail(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, name + ".jsonl " + std::to_string(lineNo) + "행 파싱 실패: " + e.what());
            }
            std::string columns, placeholders;
            int index = 0;
            for (const auto &[key, value] : row.items()) {
                (void)value;
                columns += (index == 0 ? "" : ", ") + key;
                placeholders += (index == 0 ? "?" : ", ?") + std::to_string(index + 1);
                ++index;
            }
            const std::string verb = merge ? "INSERT OR IGNORE" : "INSERT";
            auto stmt = db_.prepare(verb + " INTO " + name + " (" + columns + ") VALUES (" + placeholders + ")");
            index = 1;
            for (const auto &[key, value] : row.items()) {
                (void)key;
                bindJson(stmt, index++, value);
            }
            stmt.run();
            if (merge && db_.changes() == 0 && name != "meta") {
                if (name == "id_counter") {
                    db_.prepare("UPDATE id_counter SET next = MAX(next, ?2) WHERE prefix = ?1")
                        .bindAll(row.value("prefix", std::string()), row.value("next", int64_t{0}))
                        .run();
                } else {
                    std::string where;
                    int column = 1;
                    for (const auto &[key, value] : row.items()) {
                        (void)value;
                        where += (column == 1 ? "" : " AND ") + key + " IS ?" + std::to_string(column);
                        ++column;
                    }
                    auto same = db_.prepare("SELECT COUNT(*) FROM " + name + " WHERE " + where);
                    column = 1;
                    for (const auto &[key, value] : row.items()) {
                        (void)key;
                        bindJson(same, column++, value);
                    }
                    same.step();
                    if (same.integer(0) == 0) report.conflicts.push_back(name + ": " + row.dump());
                }
            }
            ++report.rows;
        }
    }
    savepoint.commit();
    return report;
}

std::vector<std::string> Exchange::importMarkdown(KiottySpecItemKind kind, std::string_view markdown) {
    std::vector<std::string> created;
    LegacySavepoint savepoint(db_);
    for (auto &section : splitSections(markdown, 2)) {
        std::string title = section.heading;
        KiottySpecItemCategory category = splitCategory(title);
        if (category == KIOTTY_SPEC_ITEM_CATEGORY_NONE) {
            if (kind == KIOTTY_SPEC_ITEM_KIND_REQUIREMENT) category = KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_FUNCTIONAL;
            if (kind == KIOTTY_SPEC_ITEM_KIND_RULE) category = KIOTTY_SPEC_ITEM_CATEGORY_RULE_ARCHITECTURE;
            if (kind == KIOTTY_SPEC_ITEM_KIND_INTERFACE) category = KIOTTY_SPEC_ITEM_CATEGORY_INTERFACE_C_API;
            if (kind == KIOTTY_SPEC_ITEM_KIND_DESIGN) category = KIOTTY_SPEC_ITEM_CATEGORY_DESIGN_MODULE;
        }
        std::string body;
        for (const auto line : splitLines(section.body)) {
            std::string text(line);
            if (text.rfind("###", 0) == 0) text.erase(0, 1);
            body += text + "\n";
        }
        created.push_back(expect(items_.create(kind, category, std::nullopt, title, body), failure_));
    }
    require(!created.empty(), KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "가져올 '## 제목' 절이 없음");
    savepoint.commit();
    return created;
}

std::string Exchange::queryReadonly(std::string_view sql) {
    auto stmt = db_.prepare(sql);
    require(stmt.readOnly(), KIOTTY_SPEC_STATUS_PERMISSION, "읽기 전용 SQL 만 허용됨");
    nlohmann::ordered_json rows = nlohmann::ordered_json::array();
    while (stmt.step()) {
        rows.push_back(orderedRow(stmt));
    }
    return rows.dump(2);
}

}
