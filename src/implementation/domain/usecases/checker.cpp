#include "implementation/domain/usecases/checker.h"

#include "implementation/core/markdown.h"

namespace kiotty::spec::implementation {

namespace {

constexpr KiottySpecItemKind TEST_KINDS[] = {
    KIOTTY_SPEC_ITEM_KIND_UNIT_TEST,
    KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST,
    KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST,
};

constexpr KiottySpecItemKind LINKED_KINDS[] = {
    KIOTTY_SPEC_ITEM_KIND_REQUIREMENT,
    KIOTTY_SPEC_ITEM_KIND_INTERFACE,
    KIOTTY_SPEC_ITEM_KIND_DESIGN,
};

constexpr KiottySpecDecisionKind DECISION_KINDS[] = {
    KIOTTY_SPEC_DECISION_KIND_REQUIREMENT, KIOTTY_SPEC_DECISION_KIND_RULE,      KIOTTY_SPEC_DECISION_KIND_INTERFACE,
    KIOTTY_SPEC_DECISION_KIND_DESIGN,      KIOTTY_SPEC_DECISION_KIND_UNIT_TEST, KIOTTY_SPEC_DECISION_KIND_PLAN,
};

class Report {
public:
    void failIf(bool bad, const std::string &title, const std::string &detail)
    {
        if (!bad) { return; }
        verdict.ok = false;
        verdict.report += "### " + title + "\n" + detail + "\n";
    }

    Verdict verdict;
};

class Sections {
public:
    void add(const std::string &title, const std::vector<ItemRecord> &items)
    {
        text += "## " + title + " (" + std::to_string(items.size()) + ")\n";
        text += items.empty() ? "없음\n\n" : bulletList(items) + "\n";
    }

    std::string text = "# 검사 보고서\n\n";
};

}

std::string bulletList(const std::vector<ItemRecord> &items)
{
    std::string out;
    for (const ItemRecord &item : items) { out += "- " + item.id + " " + item.title + "\n"; }
    return out;
}

bool hasAcceptanceCriteria(const std::string &body)
{
    static const std::vector<std::string> keywords{"인수 조건", "인수조건", "acceptance"};
    return countListItemsUnderHeading(body, keywords) > 0;
}

ItemListResult Checker::requirementsWithoutCriteria(int64_t cut)
{
    ItemQuery query;
    query.kind = KIOTTY_SPEC_ITEM_KIND_REQUIREMENT;
    query.category = KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_FUNCTIONAL;
    query.cut = cut;
    auto items = _s.items.list(query);
    if (!items) { return items; }
    std::vector<ItemRecord> result;
    for (ItemRecord &item : items.value) {
        if (!hasAcceptanceCriteria(item.body)) { result.push_back(std::move(item)); }
    }
    return ItemListResult::success(std::move(result));
}

OneToOneResult Checker::oneToOne(KiottySpecItemKind testKind, int64_t cut)
{
    ItemQuery query;
    query.kind = KIOTTY_SPEC_ITEM_KIND_INTERFACE;
    if (testKind == KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST) {
        query.kind = KIOTTY_SPEC_ITEM_KIND_REQUIREMENT;
        query.category = KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_FUNCTIONAL;
    }
    query.cut = cut;
    auto targets = _s.items.list(query);
    if (!targets) { return OneToOneResult::failure(targets.code); }
    OneToOne result;
    for (ItemRecord &target : targets.value) {
        const auto count = verifierCount(target, testKind, cut);
        if (!count) { return OneToOneResult::failure(count.code); }
        if (count.value == 0) { result.missing.push_back(std::move(target)); }
        else if (count.value > 1) { result.duplicated.push_back(std::move(target)); }
    }
    return OneToOneResult::success(std::move(result));
}

ItemListResult Checker::unlinked(KiottySpecItemKind kind, int64_t cut)
{
    auto items = _s.finder.ofKind(kind, cut);
    if (!items) { return items; }
    std::vector<ItemRecord> result;
    for (ItemRecord &item : items.value) {
        const auto links = _s.links.list(item.id, KIOTTY_SPEC_DIRECTION_BOTH, static_cast<KiottySpecLinkType>(0), cut);
        if (!links) { return ItemListResult::failure(links.code); }
        if (links.value.empty()) { result.push_back(std::move(item)); }
    }
    return ItemListResult::success(std::move(result));
}

ItemListResult Checker::orphanTests(int64_t cut)
{
    std::vector<ItemRecord> result;
    for (const KiottySpecItemKind kind : TEST_KINDS) {
        auto tests = _s.finder.ofKind(kind, cut);
        if (!tests) { return tests; }
        for (ItemRecord &test : tests.value) {
            const auto targeted = hasTarget(test, cut);
            if (!targeted) { return ItemListResult::failure(targeted.code); }
            if (!targeted.value) { result.push_back(std::move(test)); }
        }
    }
    return ItemListResult::success(std::move(result));
}

VerdictResult Checker::confirmCheck(int32_t step, int64_t cut)
{
    Report report;
    if (step == 1 || step == 2) {
        const auto missing = requirementsWithoutCriteria(cut);
        if (!missing) { return VerdictResult::failure(missing.code); }
        report.failIf(!missing.value.empty(), "인수 조건이 없는 기능 요구사항 (R-4)", bulletList(missing.value));
    }
    if (step == 6) {
        const auto uncovered = _s.milestones.uncovered(cut);
        if (!uncovered) { return VerdictResult::failure(uncovered.code); }
        report.failIf(!uncovered.value.empty(), "어떤 마일스톤에도 포함되지 않은 설계 항목·유닛 테스트 (R-5)", bulletList(uncovered.value));
    }
    if (step == 8 || step == 10) {
        const auto kind = step == 8 ? KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST : KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST;
        const auto check = oneToOne(kind, cut);
        if (!check) { return VerdictResult::failure(check.code); }
        const std::string what = step == 8 ? "시스템 테스트" : "E2E 테스트";
        report.failIf(!check.value.missing.empty(), what + " 가 없는 대상 (R-3)", bulletList(check.value.missing));
        report.failIf(!check.value.duplicated.empty(), what + " 가 둘 이상인 대상 (R-3)", bulletList(check.value.duplicated));
    }
    if (report.verdict.ok) { report.verdict.report = "Step " + std::to_string(step) + " 확정 조건을 만족함\n"; }
    return VerdictResult::success(std::move(report.verdict));
}

VerdictResult Checker::releaseCheck()
{
    Report report;
    for (const auto kind : {KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST, KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST}) {
        const auto check = oneToOne(kind);
        if (!check) { return VerdictResult::failure(check.code); }
        const bool violated = !check.value.missing.empty() || !check.value.duplicated.empty();
        report.failIf(violated, std::string(label(kind)) + " 1:1 위반 (R-3)", bulletList(check.value.missing) + bulletList(check.value.duplicated));
        const auto failing = notPassed(kind);
        if (!failing) { return VerdictResult::failure(failing.code); }
        report.failIf(!failing.value.empty(), std::string(label(kind)) + " 중 PASS 가 아닌 것", bulletList(failing.value));
    }
    const auto violations = _s.results.failed(KIOTTY_SPEC_ITEM_KIND_RULE);
    if (!violations) { return VerdictResult::failure(violations.code); }
    report.failIf(!violations.value.empty(), "코드 검사 위반이 남은 개발 규칙", bulletList(violations.value));
    const auto open = _s.changes.openCount();
    if (!open) { return VerdictResult::failure(open.code); }
    report.failIf(open.value > 0, "열린 변경 요청", std::to_string(open.value) + "건\n");
    if (report.verdict.ok) { report.verdict.report = "RELEASE 조건을 만족함 (R-10)\n"; }
    return VerdictResult::success(std::move(report.verdict));
}

TextResult Checker::all()
{
    Sections sections;
    const auto criteria = requirementsWithoutCriteria();
    if (!criteria) { return TextResult::failure(criteria.code); }
    sections.add("인수 조건이 없는 기능 요구사항", criteria.value);
    const auto system = oneToOne(KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST);
    if (!system) { return TextResult::failure(system.code); }
    sections.add("시스템 테스트가 없는 기능 요구사항", system.value.missing);
    sections.add("시스템 테스트가 둘 이상인 기능 요구사항", system.value.duplicated);
    const auto endToEnd = oneToOne(KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST);
    if (!endToEnd) { return TextResult::failure(endToEnd.code); }
    sections.add("E2E 테스트가 없는 외부 인터페이스 항목", endToEnd.value.missing);
    sections.add("E2E 테스트가 둘 이상인 외부 인터페이스 항목", endToEnd.value.duplicated);
    const auto uncovered = _s.milestones.uncovered();
    if (!uncovered) { return TextResult::failure(uncovered.code); }
    sections.add("마일스톤에 포함되지 않은 설계 항목·유닛 테스트", uncovered.value);
    const auto orphans = orphanTests();
    if (!orphans) { return TextResult::failure(orphans.code); }
    sections.add("검증 대상이 없는 테스트", orphans.value);
    for (const KiottySpecItemKind kind : LINKED_KINDS) {
        const auto lonely = unlinked(kind);
        if (!lonely) { return TextResult::failure(lonely.code); }
        sections.add("링크가 없는 " + std::string(label(kind)), lonely.value);
    }
    const auto failed = _s.results.failed(static_cast<KiottySpecItemKind>(0));
    if (!failed) { return TextResult::failure(failed.code); }
    sections.add("마지막 결과가 FAIL 인 테스트·규칙", failed.value);
    const auto problems = decisionProblems();
    if (!problems) { return problems; }
    return TextResult::success(sections.text + problems.value);
}

CountResult Checker::verifierCount(const ItemRecord &target, KiottySpecItemKind testKind, int64_t cut)
{
    const auto sources = _s.links.sources(target.id, KIOTTY_SPEC_LINK_TYPE_VERIFIES, cut);
    if (!sources) { return CountResult::failure(sources.code); }
    size_t count = 0;
    for (const std::string &source : sources.value) {
        const auto test = _s.items.find(source, cut);
        if (!test) { return CountResult::failure(test.code); }
        if (test.value && test.value->kind == testKind) { ++count; }
    }
    return CountResult::success(count);
}

FlagResult Checker::hasTarget(const ItemRecord &test, int64_t cut)
{
    const auto targets = _s.links.targets(test.id, KIOTTY_SPEC_LINK_TYPE_VERIFIES, cut);
    if (!targets) { return FlagResult::failure(targets.code); }
    bool found = false;
    for (const std::string &target : targets.value) {
        const auto item = _s.items.find(target, cut);
        if (!item) { return FlagResult::failure(item.code); }
        if (item.value) { found = true; }
    }
    return FlagResult::success(found);
}

ItemListResult Checker::notPassed(KiottySpecItemKind kind)
{
    auto tests = _s.finder.ofKind(kind);
    if (!tests) { return tests; }
    std::vector<ItemRecord> result;
    for (ItemRecord &test : tests.value) {
        const auto latest = _s.results.last(test.id);
        if (!latest) { return ItemListResult::failure(latest.code); }
        const bool passed = latest.value && latest.value->result == KIOTTY_SPEC_TEST_RESULT_PASS;
        if (!passed) { result.push_back(std::move(test)); }
    }
    return ItemListResult::success(std::move(result));
}

TextResult Checker::decisionProblems()
{
    std::string out = "## 결정 기록\n";
    bool clean = true;
    for (const KiottySpecDecisionKind kind : DECISION_KINDS) {
        const auto problems = _s.decisions.check(kind);
        if (!problems) { return problems; }
        if (problems.value.empty()) { continue; }
        clean = false;
        out += "### " + std::string(name(kind)) + "\n" + problems.value;
    }
    out += clean ? "문제 없음\n" : "";
    return TextResult::success(std::move(out));
}

}
