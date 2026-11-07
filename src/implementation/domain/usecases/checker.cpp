#include "implementation/domain/usecases/checker.h"

#include "implementation/core/error.h"
#include "implementation/core/markdown.h"

#include <map>

namespace kiotty::spec::implementation {

std::string bulletList(const std::vector<ItemRecord> &items) {
    std::string out;
    for (const auto &item : items) {
        out += "- " + item.id + " " + item.title + "\n";
    }
    return out;
}

bool Checker::hasAcceptanceCriteria(const std::string &body) {
    static const std::vector<std::string> keywords{"인수 조건", "인수조건", "acceptance"};
    return countListItemsUnderHeading(body, keywords) > 0;
}

std::vector<ItemRecord> Checker::requirementsWithoutCriteria(int64_t cut) {
    ItemQuery query;
    query.kind = KIOTTY_SPEC_ITEM_KIND_REQUIREMENT;
    query.category = KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_FUNCTIONAL;
    query.cut = cut;
    std::vector<ItemRecord> result;
    for (auto &item : expect(items_.list(query), failure_)) {
        if (!hasAcceptanceCriteria(item.body)) {
            result.push_back(std::move(item));
        }
    }
    return result;
}

OneToOne Checker::oneToOne(KiottySpecItemKind testKind, int64_t cut) {
    ItemQuery query;
    if (testKind == KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST) {
        query.kind = KIOTTY_SPEC_ITEM_KIND_REQUIREMENT;
        query.category = KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_FUNCTIONAL;
    } else {
        query.kind = KIOTTY_SPEC_ITEM_KIND_INTERFACE;
    }
    query.cut = cut;
    OneToOne result;
    for (auto &target : expect(items_.list(query), failure_)) {
        size_t count = 0;
        for (const auto &source : expect(links_.sources(target.id, KIOTTY_SPEC_LINK_TYPE_VERIFIES, cut), failure_)) {
            const auto test = expect(items_.find(source, cut), failure_);
            if (test && test->kind == testKind) ++count;
        }
        if (count == 0) result.missing.push_back(std::move(target));
        else if (count > 1) result.duplicated.push_back(std::move(target));
    }
    return result;
}

std::vector<ItemRecord> Checker::unlinked(KiottySpecItemKind kind, int64_t cut) {
    std::vector<ItemRecord> result;
    for (auto &item : expect(items_.ofKind(kind, cut), failure_)) {
        if (expect(links_.list(item.id, KIOTTY_SPEC_DIRECTION_BOTH, static_cast<KiottySpecLinkType>(0), cut), failure_).empty()) {
            result.push_back(std::move(item));
        }
    }
    return result;
}

std::vector<ItemRecord> Checker::orphanTests(int64_t cut) {
    std::vector<ItemRecord> result;
    for (const auto kind : {KIOTTY_SPEC_ITEM_KIND_UNIT_TEST, KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST, KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST}) {
        for (auto &test : expect(items_.ofKind(kind, cut), failure_)) {
            bool hasTarget = false;
            for (const auto &target : expect(links_.targets(test.id, KIOTTY_SPEC_LINK_TYPE_VERIFIES, cut), failure_)) {
                if (expect(items_.find(target, cut), failure_)) hasTarget = true;
            }
            if (!hasTarget) result.push_back(std::move(test));
        }
    }
    return result;
}

Verdict Checker::confirmCheck(int32_t step, int64_t cut) {
    Verdict verdict;
    const auto failIf = [&](bool bad, const std::string &title, const std::string &detail) {
        if (bad) {
            verdict.ok = false;
            verdict.report += "### " + title + "\n" + detail + "\n";
        }
    };
    if (step == 1 || step == 2) {
        const auto missing = requirementsWithoutCriteria(cut);
        failIf(!missing.empty(), "인수 조건이 없는 기능 요구사항 (R-4)", bulletList(missing));
    }
    if (step == 6) {
        const auto uncovered = milestones_.uncovered(cut);
        failIf(!uncovered.empty(), "어떤 마일스톤에도 포함되지 않은 설계 항목·유닛 테스트 (R-5)", bulletList(uncovered));
    }
    if (step == 8 || step == 10) {
        const auto kind = step == 8 ? KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST : KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST;
        const auto check = oneToOne(kind, cut);
        const std::string what = step == 8 ? "시스템 테스트" : "E2E 테스트";
        failIf(!check.missing.empty(), what + " 가 없는 대상 (R-3)", bulletList(check.missing));
        failIf(!check.duplicated.empty(), what + " 가 둘 이상인 대상 (R-3)", bulletList(check.duplicated));
    }
    if (verdict.ok) {
        verdict.report = "Step " + std::to_string(step) + " 확정 조건을 만족함\n";
    }
    return verdict;
}

Verdict Checker::releaseCheck() {
    Verdict verdict;
    const auto failIf = [&](bool bad, const std::string &title, const std::string &detail) {
        if (bad) {
            verdict.ok = false;
            verdict.report += "### " + title + "\n" + detail + "\n";
        }
    };
    for (const auto kind : {KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST, KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST}) {
        const auto check = oneToOne(kind);
        failIf(!check.missing.empty() || !check.duplicated.empty(), std::string(label(kind)) + " 1:1 위반 (R-3)",
               bulletList(check.missing) + bulletList(check.duplicated));
        std::vector<ItemRecord> notPassed;
        for (auto &test : expect(items_.ofKind(kind), failure_)) {
            const auto latest = expect(results_.last(test.id), failure_);
            if (!latest || latest->result != KIOTTY_SPEC_TEST_RESULT_PASS) notPassed.push_back(std::move(test));
        }
        failIf(!notPassed.empty(), std::string(label(kind)) + " 중 PASS 가 아닌 것", bulletList(notPassed));
    }
    const auto violations = expect(results_.failed(KIOTTY_SPEC_ITEM_KIND_RULE), failure_);
    failIf(!violations.empty(), "코드 검사 위반이 남은 개발 규칙", bulletList(violations));
    const size_t open = expect(changes_.openCount(), failure_);
    failIf(open > 0, "열린 변경 요청", std::to_string(open) + "건\n");
    if (verdict.ok) {
        verdict.report = "RELEASE 조건을 만족함 (R-10)\n";
    }
    return verdict;
}

std::string Checker::all() {
    std::string out = "# 검사 보고서\n\n";
    const auto section = [&](const std::string &title, const std::vector<ItemRecord> &items) {
        out += "## " + title + " (" + std::to_string(items.size()) + ")\n";
        out += items.empty() ? "없음\n\n" : bulletList(items) + "\n";
    };
    section("인수 조건이 없는 기능 요구사항", requirementsWithoutCriteria());
    const auto st = oneToOne(KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST);
    section("시스템 테스트가 없는 기능 요구사항", st.missing);
    section("시스템 테스트가 둘 이상인 기능 요구사항", st.duplicated);
    const auto e2e = oneToOne(KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST);
    section("E2E 테스트가 없는 외부 인터페이스 항목", e2e.missing);
    section("E2E 테스트가 둘 이상인 외부 인터페이스 항목", e2e.duplicated);
    section("마일스톤에 포함되지 않은 설계 항목·유닛 테스트", milestones_.uncovered());
    section("검증 대상이 없는 테스트", orphanTests());
    for (const auto kind : {KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, KIOTTY_SPEC_ITEM_KIND_INTERFACE, KIOTTY_SPEC_ITEM_KIND_DESIGN}) {
        section("링크가 없는 " + std::string(label(kind)), unlinked(kind));
    }
    section("마지막 결과가 FAIL 인 테스트·규칙", expect(results_.failed(static_cast<KiottySpecItemKind>(0)), failure_));
    out += "## 결정 기록\n";
    bool clean = true;
    for (const auto kind : {KIOTTY_SPEC_DECISION_KIND_REQUIREMENT, KIOTTY_SPEC_DECISION_KIND_RULE, KIOTTY_SPEC_DECISION_KIND_INTERFACE,
                            KIOTTY_SPEC_DECISION_KIND_DESIGN, KIOTTY_SPEC_DECISION_KIND_UNIT_TEST, KIOTTY_SPEC_DECISION_KIND_PLAN}) {
        const auto problems = expect(decisions_.check(kind), failure_);
        if (!problems.empty()) {
            clean = false;
            out += "### " + std::string(name(kind)) + "\n" + problems;
        }
    }
    out += clean ? "문제 없음\n" : "";
    return out;
}

}
