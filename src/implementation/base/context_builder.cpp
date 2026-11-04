#include "implementation/base/context_builder.h"

#include "implementation/core/error.h"
#include "implementation/core/text.h"

#include <set>

namespace kiotty::base {

using core::ItemRecord;

namespace {

constexpr size_t kDefaultBudget = 60000;

// 한도를 넘기 시작하면 항목을 제목만으로, 한도를 넘으면 생략 표시만 남긴다.
class Budget {
public:
    explicit Budget(size_t maxBytes) : max_(maxBytes == 0 ? kDefaultBudget : maxBytes) {}

    void text(const std::string &value) {
        if (out_.size() + value.size() <= max_) out_ += value;
        else ++omitted_;
    }

    template <class Render>
    void items(const std::vector<ItemRecord> &list, Render render) {
        if (list.empty()) {
            text("없음\n\n");
            return;
        }
        for (const auto &item : list) {
            const std::string full = render(item, true);
            if (out_.size() + full.size() <= max_ * 85 / 100) {
                out_ += full;
                continue;
            }
            const std::string brief = render(item, false);
            if (out_.size() + brief.size() <= max_) out_ += brief;
            else ++omitted_;
        }
        out_ += "\n";
    }

    std::string finish() {
        if (omitted_ > 0) {
            out_ += "\n> 한도 초과로 " + std::to_string(omitted_) + "개 조각 생략. `ks context item <ID>` 로 개별 조회할 것.\n";
        }
        return out_;
    }

private:
    size_t max_;
    size_t omitted_ = 0;
    std::string out_;
};

std::string stepTitle(int32_t step) { return std::string(core::stepLabel(step)); }

std::string stepGoal(int32_t step) {
    switch (step) {
    case 1: return "사용자 입력에서 요구사항(기능·품질·환경)과 개발 규칙을 만든다. 기능 요구사항에는 '## 인수 조건' 목록을 쓴다.";
    case 2: return "피드백대로 요구사항·개발 규칙을 고치고 요구사항·규칙 결정 기록을 다시 쓴다.";
    case 3: return "GUI 뷰·C 함수 항목과 인터페이스 파일을 만들고 satisfies / implements 링크와 인터페이스 결정 기록을 쓴다.";
    case 4: return "설계 항목과 satisfies / depends_on 링크, 설계 결정 기록을 쓴다. 인터페이스 파일은 사용자가 요청할 때만 바꾼다.";
    case 5: return "모듈마다 유닛 테스트를 만들고 verifies 링크와 유닛 테스트 결정 기록을 쓴다.";
    case 6: return "설계 항목과 유닛 테스트를 빠짐없이 포함하는 마일스톤을 만든다 (covers 링크).";
    case 7: return "다음 마일스톤을 구현하고 소스 파일을 등록·implements 링크, 포함 항목 상태, 유닛 테스트 결과를 기록한다.";
    case 8: return "기능 요구사항마다 시스템 테스트 하나를 인수 조건대로 만들고 실행 결과를 기록한다.";
    case 9: return "개발 규칙마다 소스를 검사하고 규칙별 결과(위반 위치)를 기록한다.";
    case 10: return "외부 인터페이스 항목마다 E2E 테스트 하나를 만들고 실행 결과를 기록한다.";
    default: return "";
    }
}

std::string fileList(const std::vector<core::FileRecord> &files) {
    if (files.empty()) return "없음\n\n";
    std::string out;
    for (const auto &file : files) {
        out += "- " + file.id + " [" + std::string(core::name(file.role)) + "] " + file.path + "\n";
    }
    return out + "\n";
}

} // namespace

std::string ContextBuilder::renderItem(const ItemRecord &item, bool full) {
    std::string out = "### " + item.id;
    const auto category = core::label(item.category);
    if (!category.empty()) out += " [" + std::string(category) + "]";
    out += " " + item.title + "\n";
    std::string links;
    for (const auto &link : s_.links.list(item.id, KiottySpecDirection_Upstream, static_cast<KiottySpecLinkType>(0))) {
        links += (links.empty() ? "" : ", ") + std::string(core::name(link.type)) + " " + link.toId;
    }
    if (!links.empty()) out += "> " + links + "\n";
    if (!item.parentId.empty()) out += "> parent " + item.parentId + "\n";
    if (full && !item.body.empty()) out += "\n" + item.body + "\n";
    return out + "\n";
}

std::string ContextBuilder::forStep(int32_t step, size_t maxBytes) {
    core::require(step >= 1 && step <= 10, KiottySpecStatus_InvalidArg, "step 은 1 ~ 10 이어야 함");
    Budget b(maxBytes);
    const auto render = [this](const ItemRecord &item, bool full) { return renderItem(item, full); };
    const auto decision = [&](KiottySpecDecisionKind kind) {
        const auto body = s_.decisions.get(kind);
        b.text("## 결정 기록: " + std::string(core::name(kind)) + "\n" + (body.empty() ? "없음\n" : body) + "\n\n");
    };
    const auto section = [&](const std::string &title, const std::vector<ItemRecord> &items) {
        b.text("## " + title + " (" + std::to_string(items.size()) + ")\n");
        b.items(items, render);
    };

    b.text("# Step " + std::to_string(step) + " " + stepTitle(step) + "\n\n목표: " + stepGoal(step) + "\n\n");

    if (const auto confirmed = s_.checkpoints.lastConfirmed(0)) {
        const auto changed = s_.items.changedBetween(confirmed->seq, core::kCurrent);
        if (!changed.empty()) {
            std::string list;
            for (const auto &change : changed) {
                list += "- " + change.record.id + (change.record.removed ? " (삭제)" : change.added ? " (추가)" : " (수정)") +
                        " " + change.record.title + "\n";
            }
            b.text("## " + core::checkpointName(confirmed->seq) + " 이후 바뀐 항목\n" + list + "\n");
            section("바뀐 항목과 링크된 영향 항목 (다시 볼 것)", s_.links.impactSince(confirmed->seq));
        }
    }
    const auto openChanges = s_.changes.list(KiottySpecChangeState_Open);
    if (!openChanges.empty()) {
        std::string list;
        for (const auto &change : openChanges) {
            list += "- " + change.id + " (진입 Step " + std::to_string(change.entryStep) + ") " + change.body + "\n";
        }
        b.text("## 열린 변경 요청\n" + list + "\n");
    }

    const auto requirements = s_.items.ofKind(KiottySpecItemKind_Requirement);
    const auto rulesList = s_.items.ofKind(KiottySpecItemKind_Rule);
    const auto interfaces = s_.items.ofKind(KiottySpecItemKind_Interface);
    const auto designs = s_.items.ofKind(KiottySpecItemKind_Design);
    const auto unitTests = s_.items.ofKind(KiottySpecItemKind_UnitTest);
    switch (step) {
    case 1:
        section("요구사항", requirements);
        section("개발 규칙", rulesList);
        break;
    case 2:
        section("요구사항", requirements);
        section("개발 규칙", rulesList);
        decision(KiottySpecDecisionKind_Requirement);
        decision(KiottySpecDecisionKind_Rule);
        break;
    case 3:
        section("요구사항", requirements);
        section("개발 규칙", rulesList);
        section("외부 인터페이스 항목", interfaces);
        b.text("## 인터페이스 파일\n" + fileList(s_.files.list(KiottySpecFileRole_GuiDraft)) +
               fileList(s_.files.list(KiottySpecFileRole_CHeader)));
        decision(KiottySpecDecisionKind_Interface);
        break;
    case 4:
        section("요구사항", requirements);
        section("개발 규칙", rulesList);
        section("외부 인터페이스 항목", interfaces);
        section("설계 항목", designs);
        decision(KiottySpecDecisionKind_Design);
        break;
    case 5: {
        ItemQuery testRules;
        testRules.kind = KiottySpecItemKind_Rule;
        testRules.category = KiottySpecItemCategory_RuleTest;
        section("설계 항목", designs);
        section("테스트 규칙", s_.items.list(testRules));
        section("유닛 테스트", unitTests);
        decision(KiottySpecDecisionKind_UnitTest);
        break;
    }
    case 6:
        section("설계 항목", designs);
        section("유닛 테스트", unitTests);
        section("개발 규칙", rulesList);
        section("마일스톤", s_.items.ofKind(KiottySpecItemKind_Milestone));
        section("아직 어떤 마일스톤에도 포함되지 않은 항목", s_.milestones.uncovered());
        decision(KiottySpecDecisionKind_Plan);
        break;
    case 7: {
        const auto next = s_.milestones.next();
        b.text(next ? forMilestone(*next, maxBytes / 2) : std::string("## 다음 마일스톤\n남은 마일스톤 없음\n\n"));
        b.text("## 소스 파일\n" + fileList(s_.files.list(KiottySpecFileRole_Source)));
        break;
    }
    case 8: {
        ItemQuery functional;
        functional.kind = KiottySpecItemKind_Requirement;
        functional.category = KiottySpecItemCategory_ReqFunctional;
        section("기능 요구사항", s_.items.list(functional));
        section("시스템 테스트", s_.items.ofKind(KiottySpecItemKind_SystemTest));
        b.text("## 1:1 검사\n" + s_.checker.confirmCheck(8).report + "\n");
        b.text("## 소스 파일\n" + fileList(s_.files.list(KiottySpecFileRole_Source)));
        break;
    }
    case 9:
        section("개발 규칙", rulesList);
        b.text("## 소스 파일\n" + fileList(s_.files.list(KiottySpecFileRole_Source)));
        break;
    case 10:
        section("외부 인터페이스 항목", interfaces);
        section("E2E 테스트", s_.items.ofKind(KiottySpecItemKind_E2eTest));
        b.text("## 1:1 검사\n" + s_.checker.confirmCheck(10).report + "\n");
        b.text("## 인터페이스 파일\n" + fileList(s_.files.list(KiottySpecFileRole_GuiDraft)) +
               fileList(s_.files.list(KiottySpecFileRole_CHeader)));
        b.text("## 소스 파일\n" + fileList(s_.files.list(KiottySpecFileRole_Source)));
        break;
    default:
        break;
    }
    return b.finish();
}

std::string ContextBuilder::forItem(std::string_view itemId, int32_t depth, size_t maxBytes) {
    const auto item = s_.items.get(itemId);
    Budget b(maxBytes);
    b.text("# " + item.id + " 맥락\n\n" + renderItem(item, true));
    const auto render = [this](const ItemRecord &record, bool full) { return renderItem(record, full); };
    const auto collect = [&](KiottySpecDirection dir) {
        std::vector<ItemRecord> records;
        std::string files;
        for (const auto &id : s_.links.traverse(item.id, dir, depth <= 0 ? 2 : depth)) {
            if (auto found = s_.items.find(id)) records.push_back(std::move(*found));
            else if (auto file = s_.files.find(id)) files += "- " + file->id + " " + file->path + "\n";
        }
        return std::make_pair(records, files);
    };
    const auto [up, upFiles] = collect(KiottySpecDirection_Upstream);
    b.text("## 상류 (이 항목이 만족·검증·구현하는 것)\n");
    b.items(up, render);
    const auto [down, downFiles] = collect(KiottySpecDirection_Downstream);
    b.text("## 하류 (이 항목을 만족·검증·구현하는 것)\n");
    b.items(down, render);
    if (!downFiles.empty() || !upFiles.empty()) b.text("## 연결된 파일\n" + upFiles + downFiles + "\n");

    // 이 항목을 언급하는 결정 줄만 뽑는다.
    std::string lines;
    for (const auto kind : {KiottySpecDecisionKind_Requirement, KiottySpecDecisionKind_Rule, KiottySpecDecisionKind_Interface,
                            KiottySpecDecisionKind_Design, KiottySpecDecisionKind_UnitTest, KiottySpecDecisionKind_Plan}) {
        for (const auto line : core::splitLines(s_.decisions.get(kind))) {
            if (line.find(item.id) != std::string_view::npos) lines += std::string(line) + "\n";
        }
    }
    if (!lines.empty()) b.text("## 관련 결정\n" + lines + "\n");
    if (const auto result = s_.results.last(item.id)) {
        b.text("## 마지막 결과\n" + std::string(core::name(result->result)) + "\n" + result->log + "\n");
    }
    return b.finish();
}

std::string ContextBuilder::forMilestone(std::string_view milestoneId, size_t maxBytes) {
    const auto milestone = s_.items.get(milestoneId);
    Budget b(maxBytes);
    b.text("## 마일스톤 " + milestone.id + " " + milestone.title + "\n\n" + milestone.body + "\n\n");
    std::vector<ItemRecord> todo;
    std::string states;
    std::set<std::string> requirementIds;
    for (const auto &entry : s_.milestones.entries(milestone.id)) {
        states += "- " + entry.itemId + " : " + std::string(core::name(entry.state)) + "\n";
        if (entry.state == KiottySpecImplState_Implemented) continue;
        if (auto item = s_.items.find(entry.itemId)) {
            for (const auto &target : s_.links.targets(item->id, KiottySpecLinkType_Satisfies)) requirementIds.insert(target);
            todo.push_back(std::move(*item));
        }
    }
    b.text("### 포함 항목 상태\n" + states + "\n");
    const auto render = [this](const ItemRecord &record, bool full) { return renderItem(record, full); };
    b.text("## 구현할 항목 (미구현·변경 필요)\n");
    b.items(todo, render);
    std::vector<ItemRecord> upstream;
    for (const auto &id : requirementIds) {
        if (auto item = s_.items.find(id)) upstream.push_back(std::move(*item));
    }
    b.text("## 관련 요구사항·인터페이스\n");
    b.items(upstream, [this](const ItemRecord &record, bool) { return renderItem(record, false); });
    b.text(rules());
    const auto decisions = s_.decisions.get(KiottySpecDecisionKind_Design);
    b.text("## 결정 기록: design\n" + (decisions.empty() ? std::string("없음\n") : decisions) + "\n");
    return b.finish();
}

std::string ContextBuilder::forChangeRequest(std::string_view crId, size_t maxBytes) {
    const auto change = s_.changes.get(crId);
    Budget b(maxBytes);
    b.text("# 변경 요청 " + change.id + " (" + std::string(core::name(change.source)) + ", " +
           std::string(core::name(change.state)) + ", 진입 Step " + std::to_string(change.entryStep) + ")\n\n" +
           change.body + "\n\n");
    // 본문에 등장하는 항목 ID 를 찾아 그 항목과 영향 범위를 붙인다.
    std::vector<ItemRecord> mentioned;
    for (const auto kind : {KiottySpecItemKind_Requirement, KiottySpecItemKind_Rule, KiottySpecItemKind_Interface,
                            KiottySpecItemKind_Design, KiottySpecItemKind_UnitTest, KiottySpecItemKind_SystemTest,
                            KiottySpecItemKind_E2eTest, KiottySpecItemKind_Milestone}) {
        for (auto &item : s_.items.ofKind(kind)) {
            if (change.body.find(item.id) != std::string::npos) mentioned.push_back(std::move(item));
        }
    }
    const auto render = [this](const ItemRecord &record, bool full) { return renderItem(record, full); };
    b.text("## 언급된 항목\n");
    b.items(mentioned, render);
    std::vector<ItemRecord> affected;
    std::set<std::string> seen;
    for (const auto &item : mentioned) seen.insert(item.id);
    for (const auto &item : mentioned) {
        for (const auto &id : s_.links.traverse(item.id, KiottySpecDirection_Downstream, 0)) {
            if (!seen.insert(id).second) continue;
            if (auto found = s_.items.find(id)) affected.push_back(std::move(*found));
        }
    }
    b.text("## 하류 영향 항목 (진입 단계 판단 근거)\n");
    b.items(affected, [this](const ItemRecord &record, bool) { return renderItem(record, false); });
    return b.finish();
}

std::string ContextBuilder::rules() {
    std::string out = "## 개발 규칙\n";
    const auto list = s_.items.ofKind(KiottySpecItemKind_Rule);
    if (list.empty()) return out + "없음\n\n";
    for (const auto &rule : list) out += renderItem(rule, true);
    return out;
}

std::string ContextBuilder::pending() {
    std::string out = "# 지금 할 일\n\n";
    const auto latest = s_.checkpoints.latest();
    if (latest && !latest->confirmed) {
        out += "- 미확정 체크포인트 " + core::checkpointName(latest->seq) + " (Step " + std::to_string(latest->step) +
               ") — 사용자 확정 대기\n";
    }
    const size_t writes = s_.checkpoints.pendingWriteCount();
    if (writes > 0) out += "- 아직 체크포인트에 묶이지 않은 쓰기 " + std::to_string(writes) + "건 — 라운드를 마치면 `ks checkpoint create`\n";
    for (const auto &change : s_.changes.list(static_cast<KiottySpecChangeState>(0))) {
        if (change.state == KiottySpecChangeState_Open || change.state == KiottySpecChangeState_InProgress) {
            out += "- 변경 요청 " + change.id + " (" + std::string(core::name(change.state)) + ", 진입 Step " +
                   std::to_string(change.entryStep) + ")\n";
        }
    }
    for (const auto &item : s_.results.failed(static_cast<KiottySpecItemKind>(0))) {
        out += "- FAIL " + item.id + " " + item.title + "\n";
    }
    for (const auto &milestone : s_.items.ofKind(KiottySpecItemKind_Milestone)) {
        for (const auto &entry : s_.milestones.entries(milestone.id)) {
            if (entry.state == KiottySpecImplState_NeedsChange) {
                out += "- 변경 필요 " + entry.itemId + " (" + milestone.id + ")\n";
            }
        }
    }
    return out == "# 지금 할 일\n\n" ? out + "없음\n" : out;
}

std::string ContextBuilder::status() {
    std::string out = "# 프로젝트 상태\n\n";
    const auto latest = s_.checkpoints.latest();
    const auto confirmed = s_.checkpoints.lastConfirmed(0);
    out += "- 최근 체크포인트: " + (latest ? core::checkpointName(latest->seq) + " (Step " + std::to_string(latest->step) +
                                                 (latest->confirmed ? ", 확정" : ", 미확정") + ")"
                                           : std::string("없음")) + "\n";
    out += "- 마지막 확정: " + (confirmed ? core::checkpointName(confirmed->seq) + " (Step " + std::to_string(confirmed->step) + ")"
                                          : std::string("없음")) + "\n";
    out += "\n| 종류 | 개수 |\n| --- | --- |\n";
    for (const auto kind : {KiottySpecItemKind_Requirement, KiottySpecItemKind_Rule, KiottySpecItemKind_Interface,
                            KiottySpecItemKind_Design, KiottySpecItemKind_UnitTest, KiottySpecItemKind_SystemTest,
                            KiottySpecItemKind_E2eTest, KiottySpecItemKind_Milestone}) {
        out += "| " + std::string(core::label(kind)) + " | " + std::to_string(s_.items.ofKind(kind).size()) + " |\n";
    }
    const auto progress = s_.milestones.progress();
    out += "\n- 마일스톤 포함 항목: " + std::to_string(progress.total) + " (구현 완료 " + std::to_string(progress.implemented) +
           ", 변경 필요 " + std::to_string(progress.needsChange) + ")\n";
    out += "\n| 테스트 | 전체 | PASS | FAIL | 미실행 |\n| --- | --- | --- | --- | --- |\n";
    for (const auto kind : {KiottySpecItemKind_UnitTest, KiottySpecItemKind_SystemTest, KiottySpecItemKind_E2eTest,
                            KiottySpecItemKind_Rule}) {
        const auto summary = s_.results.summary(kind);
        out += "| " + std::string(kind == KiottySpecItemKind_Rule ? "코드 검사(규칙)" : core::label(kind)) + " | " +
               std::to_string(summary.total) + " | " + std::to_string(summary.pass) + " | " + std::to_string(summary.fail) +
               " | " + std::to_string(summary.notRun) + " |\n";
    }
    out += "\n- 열린 변경 요청: " + std::to_string(s_.changes.openCount()) + "\n";
    return out;
}

} // namespace kiotty::base
