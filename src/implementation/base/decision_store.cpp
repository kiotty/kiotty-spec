#include "implementation/base/decision_store.h"

#include "implementation/core/error.h"
#include "implementation/core/text.h"

#include <map>
#include <set>

namespace kiotty::base {

namespace {

constexpr std::string_view kMiddleDot = "\xC2\xB7";  // '·'

struct DecisionLine {
    std::string itemId;
    std::string concern;
};

// 결정 줄에서 항목 ID 와 관심사를 뽑는다. 제목·빈 줄이면 false.
bool parseLine(std::string_view line, DecisionLine &out) {
    line = core::trim(line);
    if (line.empty() || line.front() == '#') {
        return false;
    }
    if (line.rfind("- ", 0) == 0 || line.rfind("* ", 0) == 0) {
        line.remove_prefix(2);
        line = core::trim(line);
    }
    size_t end = 0;
    while (end < line.size()) {
        const char c = line[end];
        const bool idChar = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
        if (!idChar) break;
        ++end;
    }
    out.itemId = std::string(line.substr(0, end));
    out.concern.clear();
    const size_t dot = line.find(kMiddleDot, end);
    if (dot != std::string_view::npos) {
        const size_t colon = line.find(':', dot);
        const size_t stop = colon == std::string_view::npos ? line.size() : colon;
        const size_t start = dot + kMiddleDot.size();
        out.concern = std::string(core::trim(line.substr(start, stop - start)));
    }
    return true;
}

} // namespace

int64_t DecisionStore::maxLine() { return meta_.policyInt("decision_max_line", 200); }
int64_t DecisionStore::maxConcerns() { return meta_.policyInt("decision_max_concerns", 8); }

std::string DecisionStore::get(KiottySpecDecisionKind kind, int64_t cut) {
    auto stmt = db_.prepare("SELECT body FROM decision_version WHERE kind = ?1 AND cp_seq <= ?2 ORDER BY cp_seq DESC LIMIT 1");
    stmt.bindAll(core::name(kind), cut);
    return stmt.step() ? stmt.text(0) : std::string();
}

std::string DecisionStore::validate(std::string_view markdown, bool checkItems) {
    std::string problems;
    std::map<std::string, std::set<std::string>> concerns;
    const auto limit = static_cast<size_t>(maxLine());
    size_t lineNo = 0;
    for (const auto line : core::splitLines(markdown)) {
        ++lineNo;
        DecisionLine parsed;
        if (!parseLine(line, parsed)) {
            continue;
        }
        const size_t length = core::utf8Length(core::trim(line));
        if (length > limit) {
            problems += "- " + std::to_string(lineNo) + "행: " + std::to_string(length) + "자 (한도 " +
                        std::to_string(limit) + "자)\n";
        }
        if (parsed.itemId.empty()) {
            problems += "- " + std::to_string(lineNo) + "행: 항목 ID 로 시작하지 않음\n";
            continue;
        }
        concerns[parsed.itemId].insert(parsed.concern);
        if (checkItems) {
            const std::string baseId = parsed.itemId.substr(0, parsed.itemId.find('.'));
            if (!items_.find(baseId)) {
                problems += "- " + std::to_string(lineNo) + "행: 존재하지 않는 항목 " + parsed.itemId + " (줄을 지울 것)\n";
            }
        }
    }
    const auto concernLimit = static_cast<size_t>(maxConcerns());
    for (const auto &[itemId, set] : concerns) {
        if (set.size() > concernLimit) {
            problems += "- " + itemId + ": 관심사 " + std::to_string(set.size()) + "개 (한도 " +
                        std::to_string(concernLimit) + "개, 관심사를 합칠 것)\n";
        }
    }
    return problems;
}

void DecisionStore::rewrite(KiottySpecDecisionKind kind, std::string_view markdown) {
    core::require(!core::name(kind).empty() && core::name(kind) != "unknown", KiottySpecStatus_InvalidArg, "알 수 없는 결정 기록 종류");
    const std::string problems = validate(markdown, false);
    core::require(problems.empty(), KiottySpecStatus_InvalidArg, "결정 기록 규칙 위반 (R-7):\n" + problems);
    db_.prepare("INSERT INTO decision_version (kind, cp_seq, body) VALUES (?1, ?2, ?3)"
                " ON CONFLICT(kind, cp_seq) DO UPDATE SET body = excluded.body")
        .bindAll(core::name(kind), core::kPending, markdown)
        .run();
}

std::string DecisionStore::check(KiottySpecDecisionKind kind) { return validate(get(kind), true); }

} // namespace kiotty::base
