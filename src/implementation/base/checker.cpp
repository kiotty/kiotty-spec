#include "implementation/base/checker.h"

#include "implementation/core/markdown.h"

#include <map>

namespace kiotty::base {

using core::ItemRecord;

std::string bulletList(const std::vector<ItemRecord> &items) {
    std::string out;
    for (const auto &item : items) {
        out += "- " + item.id + " " + item.title + "\n";
    }
    return out;
}

bool Checker::hasAcceptanceCriteria(const std::string &body) {
    static const std::vector<std::string> keywords{"인수 조건", "인수조건", "acceptance"};
    return core::markdown::countListItemsUnderHeading(body, keywords) > 0;
}

std::vector<ItemRecord> Checker::requirementsWithoutCriteria(int64_t cut) {
    ItemQuery query;
    query.kind = KiottySpecItemKind_Requirement;
    query.category = KiottySpecItemCategory_ReqFunctional;
    query.cut = cut;
    std::vector<ItemRecord> result;
    for (auto &item : items_.list(query)) {
        if (!hasAcceptanceCriteria(item.body)) {
            result.push_back(std::move(item));
        }
    }
    return result;
}

OneToOne Checker::oneToOne(KiottySpecItemKind testKind, int64_t cut) {
    ItemQuery query;
    if (testKind == KiottySpecItemKind_SystemTest) {
        query.kind = KiottySpecItemKind_Requirement;
        query.category = KiottySpecItemCategory_ReqFunctional;
    } else {
        query.kind = KiottySpecItemKind_Interface;
    }
    query.cut = cut;
    OneToOne result;
    for (auto &target : items_.list(query)) {
        size_t count = 0;
        for (const auto &source : links_.sources(target.id, KiottySpecLinkType_Verifies, cut)) {
            const auto test = items_.find(source, cut);
            if (test && test->kind == testKind) ++count;
        }
        if (count == 0) result.missing.push_back(std::move(target));
        else if (count > 1) result.duplicated.push_back(std::move(target));
    }
    return result;
}

std::vector<ItemRecord> Checker::unlinked(KiottySpecItemKind kind, int64_t cut) {
    std::vector<ItemRecord> result;
    for (auto &item : items_.ofKind(kind, cut)) {
        if (links_.list(item.id, KiottySpecDirection_Both, static_cast<KiottySpecLinkType>(0), cut).empty()) {
            result.push_back(std::move(item));
        }
    }
    return result;
}

std::vector<ItemRecord> Checker::orphanTests(int64_t cut) {
    std::vector<ItemRecord> result;
    for (const auto kind : {KiottySpecItemKind_UnitTest, KiottySpecItemKind_SystemTest, KiottySpecItemKind_E2eTest}) {
        for (auto &test : items_.ofKind(kind, cut)) {
            bool hasTarget = false;
            for (const auto &target : links_.targets(test.id, KiottySpecLinkType_Verifies, cut)) {
                if (items_.find(target, cut)) hasTarget = true;
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
        const auto kind = step == 8 ? KiottySpecItemKind_SystemTest : KiottySpecItemKind_E2eTest;
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
    for (const auto kind : {KiottySpecItemKind_SystemTest, KiottySpecItemKind_E2eTest}) {
        const auto check = oneToOne(kind);
        failIf(!check.missing.empty() || !check.duplicated.empty(), std::string(core::label(kind)) + " 1:1 위반 (R-3)",
               bulletList(check.missing) + bulletList(check.duplicated));
        std::vector<ItemRecord> notPassed;
        for (auto &test : items_.ofKind(kind)) {
            const auto latest = results_.last(test.id);
            if (!latest || latest->result != KiottySpecTestResult_Pass) notPassed.push_back(std::move(test));
        }
        failIf(!notPassed.empty(), std::string(core::label(kind)) + " 중 PASS 가 아닌 것", bulletList(notPassed));
    }
    const auto violations = results_.failed(KiottySpecItemKind_Rule);
    failIf(!violations.empty(), "코드 검사 위반이 남은 개발 규칙", bulletList(violations));
    const size_t open = changes_.openCount();
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
    const auto st = oneToOne(KiottySpecItemKind_SystemTest);
    section("시스템 테스트가 없는 기능 요구사항", st.missing);
    section("시스템 테스트가 둘 이상인 기능 요구사항", st.duplicated);
    const auto e2e = oneToOne(KiottySpecItemKind_E2eTest);
    section("E2E 테스트가 없는 외부 인터페이스 항목", e2e.missing);
    section("E2E 테스트가 둘 이상인 외부 인터페이스 항목", e2e.duplicated);
    section("마일스톤에 포함되지 않은 설계 항목·유닛 테스트", milestones_.uncovered());
    section("검증 대상이 없는 테스트", orphanTests());
    for (const auto kind : {KiottySpecItemKind_Requirement, KiottySpecItemKind_Interface, KiottySpecItemKind_Design}) {
        section("링크가 없는 " + std::string(core::label(kind)), unlinked(kind));
    }
    section("마지막 결과가 FAIL 인 테스트·규칙", results_.failed(static_cast<KiottySpecItemKind>(0)));
    out += "## 결정 기록\n";
    bool clean = true;
    for (const auto kind : {KiottySpecDecisionKind_Requirement, KiottySpecDecisionKind_Rule, KiottySpecDecisionKind_Interface,
                            KiottySpecDecisionKind_Design, KiottySpecDecisionKind_UnitTest, KiottySpecDecisionKind_Plan}) {
        const auto problems = decisions_.check(kind);
        if (!problems.empty()) {
            clean = false;
            out += "### " + std::string(core::name(kind)) + "\n" + problems;
        }
    }
    out += clean ? "문제 없음\n" : "";
    return out;
}

} // namespace kiotty::base
