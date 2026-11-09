#include "implementation/domain/usecases/kiotty_checker.h"

#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_text_view.h"

#include <string_view>
#include <vector>

namespace kiotty::spec::implementation {

namespace {

using entity::ItemHandle;
using entity::ItemKind;
using entity::ItemRecord;
using entity::RelationRecord;

constexpr uint32_t MISSING = 0xFFFFFFFFu;
constexpr size_t MAX_REPORT_LENGTH = 3800;

bool startsWithKeyword(std::string_view text)
{
    const std::string_view keywords[] = {"인수 조건", "인수조건"};
    for (const std::string_view keyword : keywords) {
        if (text.find(keyword) != std::string_view::npos) { return true; }
    }
    for (size_t index = 0; index + 10 <= text.size(); ++index) {
        bool same = true;
        const char word[] = "acceptance";
        for (size_t offset = 0; offset < 10; ++offset) {
            char letter = text[index + offset];
            if (letter >= 'A' && letter <= 'Z') { letter = static_cast<char>(letter - 'A' + 'a'); }
            if (letter != word[offset]) {
                same = false;
                break;
            }
        }
        if (same) { return true; }
    }
    return false;
}

bool isListLine(std::string_view line)
{
    size_t index = 0;
    while (index < line.size() && (line[index] == ' ' || line[index] == '\t')) { ++index; }
    if (index + 1 < line.size() && (line[index] == '-' || line[index] == '*' || line[index] == '+') && line[index + 1] == ' ') { return true; }
    size_t digits = index;
    while (digits < line.size() && line[digits] >= '0' && line[digits] <= '9') { ++digits; }
    return digits > index && digits + 1 < line.size() && (line[digits] == '.' || line[digits] == ')') && line[digits + 1] == ' ';
}

}

// 어긋난 조건과 항목 ID 를 모은다. 길면 잘라 낸다.
class Checker::Report
{
public:
    explicit Report(entity::Step step)
    {
        _text.assign(literalText("Step "));
        const char digits[3] = {static_cast<char>('0' + static_cast<int>(step) / 10), static_cast<char>('0' + static_cast<int>(step) % 10), '\0'};
        _text.append(static_cast<int>(step) >= 10 ? Span<const char>(digits, 2) : Span<const char>(digits + 1, 1));
        _text.append(literalText(" 확정 조건 미충족"));
    }

    void section(const char* title)
    {
        _pending = title;
    }

    void add(Span<const char> id)
    {
        if (_pending != nullptr) {
            append(literalText("\n- "));
            append(boundedText(_pending, 128));
            append(literalText(":"));
            _pending = nullptr;
            _failed = true;
        }
        append(literalText(" "));
        append(id);
    }

    bool failed() const { return _failed; }
    Span<const char> text() const { return _text.view(); }

private:
    void append(Span<const char> part)
    {
        if (_text.append(part) != KIOTTY_SPEC_STATUS_SUCCESS && !_truncated) {
            _truncated = true;
            _text.append(literalText(" …"));
        }
    }

    FixedString<MAX_REPORT_LENGTH> _text;
    const char* _pending {nullptr};
    bool _failed {false};
    bool _truncated {false};
};

uint64_t Checker::tagBit(ItemKind kind, const char* tag, const char* bit, uint32_t& tag_index)
{
    tag_index = MISSING;
    const auto found = _w.catalog.findTag(kind, boundedText(tag, 64));
    if (!found) { return 0; }
    const auto position = _w.catalog.findTagBit(kind, found.value, boundedText(bit, 64));
    if (!position) { return 0; }
    tag_index = found.value;
    return uint64_t{1} << position.value;
}

uint32_t Checker::relationIndex(ItemKind kind, const char* name)
{
    const auto found = _w.catalog.findRelation(kind, boundedText(name, 64));
    return found ? found.value : MISSING;
}

size_t Checker::countRelations(ItemKind model, uint32_t relation, ItemHandle item, bool as_target)
{
    size_t count = 0;
    for (const RelationRecord& record : _w.relations.all()) {
        if (record.model != model || record.relation != relation) { continue; }
        const ItemHandle side = as_target ? record.to : record.from;
        if (side.index == item.index) { ++count; }
    }
    return count;
}

bool Checker::isOpenChangeRequest(ItemHandle handle)
{
    uint32_t state = MISSING;
    const uint64_t open = tagBit(ItemKind::CHANGE_REQUEST, "상태", "열림", state);
    uint32_t ignored = MISSING;
    const uint64_t in_progress = tagBit(ItemKind::CHANGE_REQUEST, "상태", "반영 중", ignored);
    if (state == MISSING) { return true; }
    const uint64_t value = _w.items.tagValue(handle, state);
    return value == 0 || (value & (open | in_progress)) != 0;
}

size_t Checker::openChangeRequestCount()
{
    size_t count = 0;
    for (size_t index = 0; index < _w.items.capacity(); ++index) {
        const ItemHandle handle = _w.items.handleAt(index);
        const auto item = _w.items.get(handle);
        if (!item || item.value->kind != ItemKind::CHANGE_REQUEST) { continue; }
        if (isOpenChangeRequest(handle)) { ++count; }
    }
    return count;
}

uint32_t Checker::earliestEntryStep()
{
    const auto entry = _w.catalog.findTag(ItemKind::CHANGE_REQUEST, literalText("진입 단계"));
    if (!entry) { return 0; }
    uint32_t earliest = 0;
    for (size_t index = 0; index < _w.items.capacity(); ++index) {
        const ItemHandle handle = _w.items.handleAt(index);
        const auto item = _w.items.get(handle);
        if (!item || item.value->kind != ItemKind::CHANGE_REQUEST || !isOpenChangeRequest(handle)) { continue; }
        const uint64_t value = _w.items.tagValue(handle, entry.value);
        for (uint32_t bit = 0; bit < entity::STEP_COUNT; ++bit) {
            if ((value & (uint64_t{1} << bit)) == 0) { continue; }
            if (earliest == 0 || bit + 1 < earliest) { earliest = bit + 1; }
            break;
        }
    }
    return earliest;
}

size_t Checker::countAcceptanceCriteria(Span<const char> body)
{
    const std::string_view text = viewOf(body);
    size_t count = 0;
    size_t active_level = 0;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) { end = text.size(); }
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') { line.remove_suffix(1); }
        size_t level = 0;
        while (level < line.size() && line[level] == '#') { ++level; }
        if (level > 0 && level <= 6 && level < line.size() && line[level] == ' ') {
            if (active_level != 0 && level <= active_level) { active_level = 0; }
            if (startsWithKeyword(line.substr(level))) { active_level = level; }
        } else if (active_level != 0 && isListLine(line)) {
            ++count;
        }
        if (end == text.size()) { break; }
        start = end + 1;
    }
    return count;
}

void Checker::checkEntrySteps(Report& report)
{
    report.section("진입 단계가 정해지지 않은 열린 변경 요청");
    const auto entry = _w.catalog.findTag(ItemKind::CHANGE_REQUEST, literalText("진입 단계"));
    for (size_t index = 0; index < _w.items.capacity(); ++index) {
        const ItemHandle handle = _w.items.handleAt(index);
        const auto item = _w.items.get(handle);
        if (!item || item.value->kind != ItemKind::CHANGE_REQUEST || !isOpenChangeRequest(handle)) { continue; }
        if (entry && _w.items.tagValue(handle, entry.value) != 0) { continue; }
        report.add(item.value->id.view());
    }
}

void Checker::checkAcceptanceCriteria(Report& report)
{
    report.section("인수 조건이 없는 기능 요구사항 (R-4)");
    uint32_t kind_tag = MISSING;
    const uint64_t functional = tagBit(ItemKind::REQUIREMENT, "요구사항 종류", "기능 요구사항", kind_tag);
    if (kind_tag == MISSING) { return; }
    std::vector<char>& buffer = _body;
    for (size_t index = 0; index < _w.items.capacity(); ++index) {
        const ItemHandle handle = _w.items.handleAt(index);
        const auto item = _w.items.get(handle);
        if (!item || item.value->kind != ItemKind::REQUIREMENT) { continue; }
        if ((_w.items.tagValue(handle, kind_tag) & functional) == 0) { continue; }
        const auto length = _w.items.readBody(handle, buffer.data(), buffer.size());
        if (!length || countAcceptanceCriteria(Span<const char>(buffer.data(), length.value)) == 0) { report.add(item.value->id.view()); }
    }
}

void Checker::checkMilestoneCoverage(Report& report)
{
    report.section("어떤 마일스톤에도 포함되지 않은 설계 사항 · 유닛 테스트 (R-5)");
    const uint32_t designs = relationIndex(ItemKind::MILESTONE, "포함 설계 사항");
    const uint32_t tests = relationIndex(ItemKind::MILESTONE, "포함 유닛 테스트");
    for (size_t index = 0; index < _w.items.capacity(); ++index) {
        const ItemHandle handle = _w.items.handleAt(index);
        const auto item = _w.items.get(handle);
        if (!item) { continue; }
        uint32_t relation = MISSING;
        if (item.value->kind == ItemKind::DESIGN) { relation = designs; }
        if (item.value->kind == ItemKind::UNIT_TEST_PLAN) { relation = tests; }
        if (relation == MISSING) { continue; }
        if (countRelations(ItemKind::MILESTONE, relation, handle, true) == 0) { report.add(item.value->id.view()); }
    }
}

void Checker::checkOneToOne(Report& report, ItemKind target_kind, ItemKind test_kind, const char* relation_name, bool functional_only)
{
    const uint32_t relation = relationIndex(test_kind, relation_name);
    uint32_t kind_tag = MISSING;
    const uint64_t functional = functional_only ? tagBit(ItemKind::REQUIREMENT, "요구사항 종류", "기능 요구사항", kind_tag) : 0;
    report.section(test_kind == ItemKind::SYSTEM_TEST_PLAN ? "시스템 테스트와 1:1 이 아닌 기능 요구사항 (R-3)"
                   : test_kind == ItemKind::END_TO_END_TEST_PLAN ? "E2E 테스트와 1:1 이 아닌 외부 인터페이스 (R-3)"
                                                                 : "코드 검사 결과와 1:1 이 아닌 개발 수칙");
    for (size_t index = 0; index < _w.items.capacity(); ++index) {
        const ItemHandle handle = _w.items.handleAt(index);
        const auto item = _w.items.get(handle);
        if (!item || item.value->kind != target_kind) { continue; }
        if (functional_only && (kind_tag == MISSING || (_w.items.tagValue(handle, kind_tag) & functional) == 0)) { continue; }
        if (countRelations(test_kind, relation, handle, true) != 1) { report.add(item.value->id.view()); }
    }
    report.section("대상이 하나가 아닌 테스트 · 검사 항목");
    for (size_t index = 0; index < _w.items.capacity(); ++index) {
        const ItemHandle handle = _w.items.handleAt(index);
        const auto item = _w.items.get(handle);
        if (!item || item.value->kind != test_kind) { continue; }
        if (countRelations(test_kind, relation, handle, false) != 1) { report.add(item.value->id.view()); }
    }
}

void Checker::checkResults(Report& report)
{
    report.section("PASS 가 아닌 결과 (R-10)");
    const ItemKind kinds[] = {ItemKind::SYSTEM_TEST_RESULT, ItemKind::END_TO_END_TEST_RESULT, ItemKind::CODE_INSPECTION_RESULT};
    for (size_t index = 0; index < _w.items.capacity(); ++index) {
        const ItemHandle handle = _w.items.handleAt(index);
        const auto item = _w.items.get(handle);
        if (!item) { continue; }
        for (const ItemKind kind : kinds) {
            if (item.value->kind != kind) { continue; }
            uint32_t result = MISSING;
            const uint64_t pass = tagBit(kind, "결과", "PASS", result);
            if (result == MISSING || _w.items.tagValue(handle, result) != pass) { report.add(item.value->id.view()); }
        }
    }
    report.section("열린 변경 요청 (R-10)");
    for (size_t index = 0; index < _w.items.capacity(); ++index) {
        const ItemHandle handle = _w.items.handleAt(index);
        const auto item = _w.items.get(handle);
        if (item && item.value->kind == ItemKind::CHANGE_REQUEST && isOpenChangeRequest(handle)) { report.add(item.value->id.view()); }
    }
}

KiottySpecStatus Checker::check(entity::Step step)
{
    Report report(step);
    switch (step) {
    case entity::Step::CHANGE_REQUEST_REPORTED: checkEntrySteps(report); break;
    case entity::Step::REQUIREMENT_DISCUSSING: checkAcceptanceCriteria(report); break;
    case entity::Step::DEVELOPMENT_PLAN_DISCUSSING: checkMilestoneCoverage(report); break;
    case entity::Step::SYSTEM_TEST_PLAN_DISCUSSING:
        checkOneToOne(report, ItemKind::REQUIREMENT, ItemKind::SYSTEM_TEST_PLAN, "검증 요구사항", true);
        break;
    case entity::Step::CODE_INSPECTING: checkOneToOne(report, ItemKind::RULE, ItemKind::CODE_INSPECTION_RESULT, "검사 개발 수칙", false); break;
    case entity::Step::END_TO_END_TEST_DISCUSSING:
        checkOneToOne(report, ItemKind::EXTERNAL_INTERFACE, ItemKind::END_TO_END_TEST_PLAN, "검증 외부 인터페이스", false);
        break;
    case entity::Step::END_TO_END_TEST_RUN: checkResults(report); break;
    default: break;
    }
    if (!report.failed()) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _w.failure.raise(KIOTTY_SPEC_STATUS_PRECONDITION, {report.text()});
}

}
