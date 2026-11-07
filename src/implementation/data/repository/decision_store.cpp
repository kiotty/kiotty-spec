#include "implementation/data/repository/decision_store.h"

#include "implementation/core/text.h"
#include "implementation/core/text_view.h"

#include <map>
#include <set>
#include <string>

namespace kiotty::spec::implementation {

namespace {

constexpr std::string_view MIDDLE_DOT = "\xC2\xB7";
constexpr int64_t DEFAULT_MAX_LINE = 200;
constexpr int64_t DEFAULT_MAX_CONCERNS = 8;
constexpr char MAX_LINE_KEY[] = "decision_max_line";
constexpr char MAX_CONCERNS_KEY[] = "decision_max_concerns";

constexpr char SELECT_SQL[] =
    "SELECT body FROM decision_version WHERE kind = ?1 AND cp_seq <= ?2 ORDER BY cp_seq DESC LIMIT 1";
constexpr char WRITE_SQL[] =
    "INSERT INTO decision_version (kind, cp_seq, body) VALUES (?1, ?2, ?3)"
    " ON CONFLICT(kind, cp_seq) DO UPDATE SET body = excluded.body";

struct DecisionLine {
    std::string itemId;
    std::string concern;
};

bool isIdCharacter(char c)
{
    const bool isUpper = c >= 'A' && c <= 'Z';
    const bool isLower = c >= 'a' && c <= 'z';
    const bool isDigit = c >= '0' && c <= '9';
    return isUpper || isLower || isDigit || c == '-' || c == '.';
}

std::string_view dropBullet(std::string_view line)
{
    const bool isBullet = line.rfind("- ", 0) == 0 || line.rfind("* ", 0) == 0;
    if (!isBullet) { return line; }
    return trim(line.substr(2));
}

std::string concernOf(std::string_view line, size_t from)
{
    const size_t dot = line.find(MIDDLE_DOT, from);
    if (dot == std::string_view::npos) { return std::string(); }
    const size_t colon = line.find(':', dot);
    const size_t stop = colon == std::string_view::npos ? line.size() : colon;
    const size_t start = dot + MIDDLE_DOT.size();
    return std::string(trim(line.substr(start, stop - start)));
}

std::optional<DecisionLine> parseLine(std::string_view raw)
{
    const std::string_view trimmed = trim(raw);
    if (trimmed.empty() || trimmed.front() == '#') { return std::nullopt; }
    const std::string_view line = dropBullet(trimmed);
    size_t end = 0;
    while (end < line.size() && isIdCharacter(line[end])) { ++end; }
    DecisionLine parsed;
    parsed.itemId = std::string(line.substr(0, end));
    parsed.concern = concernOf(line, end);
    return parsed;
}

}

TextResult DecisionStore::get(KiottySpecDecisionKind kind, int64_t cut)
{
    auto query = _storage.prepare(literalText(SELECT_SQL));
    if (!query) { return TextResult::failure(query.code); }
    query.value.bind(1, spanOf(name(kind)));
    query.value.bind(2, cut);
    const auto stepped = query.value.step();
    if (!stepped) { return TextResult::failure(_storage.check(stepped.code)); }
    if (!stepped.value) { return TextResult::success(std::string()); }
    return TextResult::success(std::string(viewOf(query.value.row().text(0))));
}

KiottySpecStatus DecisionStore::rewrite(KiottySpecDecisionKind kind, std::string_view markdown)
{
    const bool isKnown = !name(kind).empty() && name(kind) != "unknown";
    if (!isKnown) { return _storage.reject(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("알 수 없는 결정 기록 종류")}); }
    const auto problems = validate(markdown, false);
    if (!problems) { return problems.code; }
    if (!problems.value.empty()) { return _storage.reject(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("결정 기록 규칙 위반 (R-7):\n"), spanOf(problems.value)}); }
    return write(kind, markdown);
}

TextResult DecisionStore::check(KiottySpecDecisionKind kind)
{
    const auto body = get(kind);
    if (!body) { return body; }
    return validate(body.value, true);
}

SequenceResult DecisionStore::maxLine() { return _meta.policyInt(MAX_LINE_KEY, DEFAULT_MAX_LINE); }

SequenceResult DecisionStore::maxConcerns() { return _meta.policyInt(MAX_CONCERNS_KEY, DEFAULT_MAX_CONCERNS); }

TextResult DecisionStore::validate(std::string_view markdown, bool checkItems)
{
    const auto lineLimit = maxLine();
    if (!lineLimit) { return TextResult::failure(lineLimit.code); }
    const auto concernLimit = maxConcerns();
    if (!concernLimit) { return TextResult::failure(concernLimit.code); }
    const size_t limit = static_cast<size_t>(lineLimit.value);
    std::string problems;
    std::map<std::string, std::set<std::string>> concerns;
    size_t lineNo = 0;
    for (const std::string_view line : splitLines(markdown)) {
        ++lineNo;
        const auto parsed = parseLine(line);
        if (!parsed) { continue; }
        const size_t length = utf8Length(trim(line));
        if (length > limit) {
            problems += "- " + std::to_string(lineNo) + "행: " + std::to_string(length) + "자 (한도 " + std::to_string(limit) + "자)\n";
        }
        if (parsed->itemId.empty()) {
            problems += "- " + std::to_string(lineNo) + "행: 항목 ID 로 시작하지 않음\n";
            continue;
        }
        concerns[parsed->itemId].insert(parsed->concern);
        if (!checkItems) { continue; }
        const std::string baseId = parsed->itemId.substr(0, parsed->itemId.find('.'));
        const auto item = _items.find(baseId);
        if (!item) { return TextResult::failure(item.code); }
        if (!item.value) { problems += "- " + std::to_string(lineNo) + "행: 존재하지 않는 항목 " + parsed->itemId + " (줄을 지울 것)\n"; }
    }
    const size_t maxConcernCount = static_cast<size_t>(concernLimit.value);
    for (const auto &[itemId, set] : concerns) {
        if (set.size() <= maxConcernCount) { continue; }
        problems += "- " + itemId + ": 관심사 " + std::to_string(set.size()) + "개 (한도 " + std::to_string(maxConcernCount) + "개, 관심사를 합칠 것)\n";
    }
    return TextResult::success(std::move(problems));
}

KiottySpecStatus DecisionStore::write(KiottySpecDecisionKind kind, std::string_view markdown)
{
    auto query = _storage.prepare(literalText(WRITE_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, spanOf(name(kind)));
    query.value.bind(2, kPending);
    query.value.bind(3, spanOf(markdown));
    return _storage.check(query.value.run());
}

}
