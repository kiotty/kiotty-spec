#include "implementation/base/exchange.h"

#include "implementation/base/schema.h"
#include "implementation/core/error.h"
#include "implementation/core/markdown.h"
#include "implementation/core/text.h"

#include <nlohmann/json.hpp>

#include <map>

namespace kiotty::base {

using nlohmann::json;

namespace {

json rowToJson(const core::Statement &stmt) {
    json row = json::object();
    for (int i = 0; i < stmt.columnCount(); ++i) {
        const std::string column = stmt.columnName(i);
        switch (stmt.columnType(i)) {
        case core::ColumnType::Integer: row[column] = stmt.integer(i); break;
        case core::ColumnType::Float: row[column] = stmt.real(i); break;
        case core::ColumnType::Null: row[column] = nullptr; break;
        default: row[column] = stmt.text(i); break;
        }
    }
    return row;
}

void bindJson(core::Statement &stmt, int index, const json &value) {
    if (value.is_null()) stmt.bindOne(index, nullptr);
    else if (value.is_number_integer()) stmt.bindOne(index, value.get<int64_t>());
    else if (value.is_number()) stmt.bindOne(index, value.get<double>());
    else if (value.is_boolean()) stmt.bindOne(index, value.get<bool>());
    else if (value.is_string()) stmt.bindOne(index, value.get<std::string>());
    else stmt.bindOne(index, value.dump());
}

// "[functional] 제목" -> category, 제목
KiottySpecItemCategory splitCategory(std::string &heading) {
    KiottySpecItemCategory category = KiottySpecItemCategory_None;
    const std::string_view trimmed = core::trim(heading);
    if (!trimmed.empty() && trimmed.front() == '[') {
        const size_t close = trimmed.find(']');
        if (close != std::string_view::npos) {
            const std::string tag(core::trim(trimmed.substr(1, close - 1)));
            core::parse(tag, category);
            heading = std::string(core::trim(trimmed.substr(close + 1)));
        }
    }
    return category;
}

} // namespace

std::string Exchange::exportMarkdown(KiottySpecItemKind kind) {
    std::string out = "# " + std::string(core::label(kind)) + "\n\n";
    const auto items = items_.ofKind(kind);
    if (items.empty()) return out + "없음\n";
    for (const auto &item : items) {
        out += context_.renderItem(item, true);
    }
    return out;
}

std::string Exchange::traceReport() {
    std::string out = "# 추적 보고서\n\n";
    const std::vector<std::pair<KiottySpecItemKind, KiottySpecItemKind>> pairs{
        {KiottySpecItemKind_Requirement, KiottySpecItemKind_Interface},
        {KiottySpecItemKind_Requirement, KiottySpecItemKind_Design},
        {KiottySpecItemKind_Interface, KiottySpecItemKind_Design},
        {KiottySpecItemKind_Design, KiottySpecItemKind_UnitTest},
        {KiottySpecItemKind_Requirement, KiottySpecItemKind_SystemTest},
        {KiottySpecItemKind_Interface, KiottySpecItemKind_E2eTest},
        {KiottySpecItemKind_Milestone, KiottySpecItemKind_Design},
    };
    for (const auto &[row, col] : pairs) {
        out += "## " + std::string(core::label(row)) + " × " + std::string(core::label(col)) + "\n```csv\n" +
               links_.matrix(row, col) + "```\n\n";
    }
    return out + checker_.all();
}

void Exchange::exportJsonl(const std::filesystem::path &dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    core::require(!ec, KiottySpecStatus_Io, "디렉토리를 만들 수 없음: " + core::pathToUtf8(dir));
    for (const auto table : schema::allTables()) {
        auto stmt = db_.prepare("SELECT * FROM " + std::string(table) + " ORDER BY " + std::string(schema::primaryKeyOrder(table)));
        std::string content;
        while (stmt.step()) {
            content += rowToJson(stmt).dump() + "\n";
        }
        core::writeFile(dir / (std::string(table) + ".jsonl"), content);
    }
}

ImportReport Exchange::importJsonl(const std::filesystem::path &dir, bool merge) {
    ImportReport report;
    core::Savepoint savepoint(db_);
    for (const auto table : schema::allTables()) {
        const auto content = core::readFile(dir / (std::string(table) + ".jsonl"));
        if (!content) continue;
        const std::string name(table);
        if (!merge) db_.exec("DELETE FROM " + name);
        size_t lineNo = 0;
        for (const auto line : core::splitLines(*content)) {
            ++lineNo;
            if (core::trim(line).empty()) continue;
            json row;
            try {
                row = json::parse(line);
            } catch (const std::exception &e) {
                core::fail(KiottySpecStatus_InvalidArg, name + ".jsonl " + std::to_string(lineNo) + "행 파싱 실패: " + e.what());
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
                // 같은 키가 이미 있다. id_counter 는 큰 값을 택하고, 나머지는 기존 값을 유지한 채 보고한다.
                if (name == "id_counter") {
                    db_.prepare("UPDATE id_counter SET next = MAX(next, ?2) WHERE prefix = ?1")
                        .bindAll(row.value("prefix", std::string()), row.value("next", int64_t{0}))
                        .run();
                } else {
                    // 모든 열이 같은 행이 이미 있으면 충돌이 아니다.
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
    core::Savepoint savepoint(db_);
    for (auto &section : core::markdown::splitSections(markdown, 2)) {
        std::string title = section.heading;
        KiottySpecItemCategory category = splitCategory(title);
        if (category == KiottySpecItemCategory_None) {
            // 분류를 쓰지 않았으면 종류의 기본 분류를 쓴다.
            if (kind == KiottySpecItemKind_Requirement) category = KiottySpecItemCategory_ReqFunctional;
            if (kind == KiottySpecItemKind_Rule) category = KiottySpecItemCategory_RuleArchitecture;
            if (kind == KiottySpecItemKind_Interface) category = KiottySpecItemCategory_UiCApi;
            if (kind == KiottySpecItemKind_Design) category = KiottySpecItemCategory_DsnModule;
        }
        // 제목 아래 본문의 ### 를 ## 로 한 단계 올려 항목 본문 틀과 맞춘다.
        std::string body;
        for (const auto line : core::splitLines(section.body)) {
            std::string text(line);
            if (text.rfind("###", 0) == 0) text.erase(0, 1);
            body += text + "\n";
        }
        created.push_back(items_.create(kind, category, std::nullopt, title, body));
    }
    core::require(!created.empty(), KiottySpecStatus_InvalidArg, "가져올 '## 제목' 절이 없음");
    savepoint.commit();
    return created;
}

std::string Exchange::queryReadonly(std::string_view sql) {
    auto stmt = db_.prepare(sql);
    core::require(stmt.readOnly(), KiottySpecStatus_Permission, "읽기 전용 SQL 만 허용됨");
    json rows = json::array();
    while (stmt.step()) {
        rows.push_back(rowToJson(stmt));
    }
    return rows.dump(2);
}

} // namespace kiotty::base
