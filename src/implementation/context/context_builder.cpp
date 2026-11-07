#include "implementation/context/context_builder.h"

#include "implementation/core/error.h"
#include "implementation/core/text.h"

#include <set>

namespace kiotty::spec::implementation {

namespace {

constexpr size_t kDefaultBudget = 60000;

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

std::string stepTitle(int32_t step) { return std::string(stepLabel(step)); }

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

std::string fileList(const std::vector<FileRecord> &files) {
    if (files.empty()) return "없음\n\n";
    std::string out;
    for (const auto &file : files) {
        out += "- " + file.id + " [" + std::string(name(file.role)) + "] " + file.path + "\n";
    }
    return out + "\n";
}

}

std::string ContextBuilder::renderItem(const ItemRecord &item, bool full) {
    std::string out = "### " + item.id;
    const auto category = label(item.category);
    if (!category.empty()) out += " [" + std::string(category) + "]";
    out += " " + item.title + "\n";
    std::string links;
    for (const auto &link : expect(s_.links.list(item.id, KIOTTY_SPEC_DIRECTION_UPSTREAM, static_cast<KiottySpecLinkType>(0)), s_.failure)) {
        links += (links.empty() ? "" : ", ") + std::string(name(link.type)) + " " + link.toId;
    }
    if (!links.empty()) out += "> " + links + "\n";
    if (!item.parentId.empty()) out += "> parent " + item.parentId + "\n";
    if (full && !item.body.empty()) out += "\n" + item.body + "\n";
    return out + "\n";
}

std::string ContextBuilder::forStep(int32_t step, size_t maxBytes) {
    require(step >= 1 && step <= 10, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "step 은 1 ~ 10 이어야 함");
    Budget b(maxBytes);
    const auto render = [this](const ItemRecord &item, bool full) { return renderItem(item, full); };
    const auto decision = [&](KiottySpecDecisionKind kind) {
        const auto body = expect(s_.decisions.get(kind), s_.failure);
        b.text("## 결정 기록: " + std::string(name(kind)) + "\n" + (body.empty() ? "없음\n" : body) + "\n\n");
    };
    const auto section = [&](const std::string &title, const std::vector<ItemRecord> &items) {
        b.text("## " + title + " (" + std::to_string(items.size()) + ")\n");
        b.items(items, render);
    };

    b.text("# Step " + std::to_string(step) + " " + stepTitle(step) + "\n\n목표: " + stepGoal(step) + "\n\n");

    if (const auto confirmed = expect(s_.checkpoints.lastConfirmed(0), s_.failure)) {
        const auto changed = expect(s_.finder.changedBetween(confirmed->seq, kCurrent), s_.failure);
        if (!changed.empty()) {
            std::string list;
            for (const auto &change : changed) {
                list += "- " + change.record.id + (change.record.removed ? " (삭제)" : change.added ? " (추가)" : " (수정)") +
                        " " + change.record.title + "\n";
            }
            b.text("## " + checkpointName(confirmed->seq) + " 이후 바뀐 항목\n" + list + "\n");
            section("바뀐 항목과 링크된 영향 항목 (다시 볼 것)", expect(s_.graph.impactSince(confirmed->seq), s_.failure));
        }
    }
    const auto openChanges = expect(s_.changes.list(KIOTTY_SPEC_CHANGE_STATE_OPEN), s_.failure);
    if (!openChanges.empty()) {
        std::string list;
        for (const auto &change : openChanges) {
            list += "- " + change.id + " (진입 Step " + std::to_string(change.entryStep) + ") " + change.body + "\n";
        }
        b.text("## 열린 변경 요청\n" + list + "\n");
    }

    const auto requirements = expect(s_.finder.ofKind(KIOTTY_SPEC_ITEM_KIND_REQUIREMENT), s_.failure);
    const auto rulesList = expect(s_.finder.ofKind(KIOTTY_SPEC_ITEM_KIND_RULE), s_.failure);
    const auto interfaces = expect(s_.finder.ofKind(KIOTTY_SPEC_ITEM_KIND_INTERFACE), s_.failure);
    const auto designs = expect(s_.finder.ofKind(KIOTTY_SPEC_ITEM_KIND_DESIGN), s_.failure);
    const auto unitTests = expect(s_.finder.ofKind(KIOTTY_SPEC_ITEM_KIND_UNIT_TEST), s_.failure);
    switch (step) {
    case 1:
        section("요구사항", requirements);
        section("개발 규칙", rulesList);
        break;
    case 2:
        section("요구사항", requirements);
        section("개발 규칙", rulesList);
        decision(KIOTTY_SPEC_DECISION_KIND_REQUIREMENT);
        decision(KIOTTY_SPEC_DECISION_KIND_RULE);
        break;
    case 3:
        section("요구사항", requirements);
        section("개발 규칙", rulesList);
        section("외부 인터페이스 항목", interfaces);
        b.text("## 인터페이스 파일\n" + fileList(expect(s_.files.list(KIOTTY_SPEC_FILE_ROLE_GUI_DRAFT), s_.failure)) +
               fileList(expect(s_.files.list(KIOTTY_SPEC_FILE_ROLE_C_HEADER), s_.failure)));
        decision(KIOTTY_SPEC_DECISION_KIND_INTERFACE);
        break;
    case 4:
        section("요구사항", requirements);
        section("개발 규칙", rulesList);
        section("외부 인터페이스 항목", interfaces);
        section("설계 항목", designs);
        decision(KIOTTY_SPEC_DECISION_KIND_DESIGN);
        break;
    case 5: {
        ItemQuery testRules;
        testRules.kind = KIOTTY_SPEC_ITEM_KIND_RULE;
        testRules.category = KIOTTY_SPEC_ITEM_CATEGORY_RULE_TEST;
        section("설계 항목", designs);
        section("테스트 규칙", expect(s_.items.list(testRules), s_.failure));
        section("유닛 테스트", unitTests);
        decision(KIOTTY_SPEC_DECISION_KIND_UNIT_TEST);
        break;
    }
    case 6:
        section("설계 항목", designs);
        section("유닛 테스트", unitTests);
        section("개발 규칙", rulesList);
        section("마일스톤", expect(s_.finder.ofKind(KIOTTY_SPEC_ITEM_KIND_MILESTONE), s_.failure));
        section("아직 어떤 마일스톤에도 포함되지 않은 항목", expect(s_.milestones.uncovered(), s_.failure));
        decision(KIOTTY_SPEC_DECISION_KIND_PLAN);
        break;
    case 7: {
        const auto next = expect(s_.milestones.next(), s_.failure);
        b.text(next ? forMilestone(*next, maxBytes / 2) : std::string("## 다음 마일스톤\n남은 마일스톤 없음\n\n"));
        b.text("## 소스 파일\n" + fileList(expect(s_.files.list(KIOTTY_SPEC_FILE_ROLE_SOURCE), s_.failure)));
        break;
    }
    case 8: {
        ItemQuery functional;
        functional.kind = KIOTTY_SPEC_ITEM_KIND_REQUIREMENT;
        functional.category = KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_FUNCTIONAL;
        section("기능 요구사항", expect(s_.items.list(functional), s_.failure));
        section("시스템 테스트", expect(s_.finder.ofKind(KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST), s_.failure));
        b.text("## 1:1 검사\n" + expect(s_.checker.confirmCheck(8), s_.failure).report + "\n");
        b.text("## 소스 파일\n" + fileList(expect(s_.files.list(KIOTTY_SPEC_FILE_ROLE_SOURCE), s_.failure)));
        break;
    }
    case 9:
        section("개발 규칙", rulesList);
        b.text("## 소스 파일\n" + fileList(expect(s_.files.list(KIOTTY_SPEC_FILE_ROLE_SOURCE), s_.failure)));
        break;
    case 10:
        section("외부 인터페이스 항목", interfaces);
        section("E2E 테스트", expect(s_.finder.ofKind(KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST), s_.failure));
        b.text("## 1:1 검사\n" + expect(s_.checker.confirmCheck(10), s_.failure).report + "\n");
        b.text("## 인터페이스 파일\n" + fileList(expect(s_.files.list(KIOTTY_SPEC_FILE_ROLE_GUI_DRAFT), s_.failure)) +
               fileList(expect(s_.files.list(KIOTTY_SPEC_FILE_ROLE_C_HEADER), s_.failure)));
        b.text("## 소스 파일\n" + fileList(expect(s_.files.list(KIOTTY_SPEC_FILE_ROLE_SOURCE), s_.failure)));
        break;
    default:
        break;
    }
    return b.finish();
}

std::string ContextBuilder::forItem(std::string_view itemId, int32_t depth, size_t maxBytes) {
    const auto item = expect(s_.items.get(itemId), s_.failure);
    Budget b(maxBytes);
    b.text("# " + item.id + " 맥락\n\n" + renderItem(item, true));
    const auto render = [this](const ItemRecord &record, bool full) { return renderItem(record, full); };
    const auto collect = [&](KiottySpecDirection dir) {
        std::vector<ItemRecord> records;
        std::string files;
        for (const auto &id : expect(s_.graph.traverse(item.id, dir, depth <= 0 ? 2 : depth), s_.failure)) {
            if (auto found = expect(s_.items.find(id), s_.failure)) records.push_back(std::move(*found));
            else if (auto file = expect(s_.files.find(id), s_.failure)) files += "- " + file->id + " " + file->path + "\n";
        }
        return std::make_pair(records, files);
    };
    const auto [up, upFiles] = collect(KIOTTY_SPEC_DIRECTION_UPSTREAM);
    b.text("## 상류 (이 항목이 만족·검증·구현하는 것)\n");
    b.items(up, render);
    const auto [down, downFiles] = collect(KIOTTY_SPEC_DIRECTION_DOWNSTREAM);
    b.text("## 하류 (이 항목을 만족·검증·구현하는 것)\n");
    b.items(down, render);
    if (!downFiles.empty() || !upFiles.empty()) b.text("## 연결된 파일\n" + upFiles + downFiles + "\n");

    std::string lines;
    for (const auto kind : {KIOTTY_SPEC_DECISION_KIND_REQUIREMENT, KIOTTY_SPEC_DECISION_KIND_RULE, KIOTTY_SPEC_DECISION_KIND_INTERFACE,
                            KIOTTY_SPEC_DECISION_KIND_DESIGN, KIOTTY_SPEC_DECISION_KIND_UNIT_TEST, KIOTTY_SPEC_DECISION_KIND_PLAN}) {
        const std::string body = expect(s_.decisions.get(kind), s_.failure);
        for (const auto line : splitLines(body)) {
            if (line.find(item.id) != std::string_view::npos) lines += std::string(line) + "\n";
        }
    }
    if (!lines.empty()) b.text("## 관련 결정\n" + lines + "\n");
    if (const auto result = expect(s_.results.last(item.id), s_.failure)) {
        b.text("## 마지막 결과\n" + std::string(name(result->result)) + "\n" + result->log + "\n");
    }
    return b.finish();
}

std::string ContextBuilder::forMilestone(std::string_view milestoneId, size_t maxBytes) {
    const auto milestone = expect(s_.items.get(milestoneId), s_.failure);
    Budget b(maxBytes);
    b.text("## 마일스톤 " + milestone.id + " " + milestone.title + "\n\n" + milestone.body + "\n\n");
    std::vector<ItemRecord> todo;
    std::string states;
    std::set<std::string> requirementIds;
    for (const auto &entry : expect(s_.milestones.entries(milestone.id), s_.failure)) {
        states += "- " + entry.itemId + " : " + std::string(name(entry.state)) + "\n";
        if (entry.state == KIOTTY_SPEC_IMPLEMENTATION_STATE_IMPLEMENTED) continue;
        if (auto item = expect(s_.items.find(entry.itemId), s_.failure)) {
            for (const auto &target : expect(s_.links.targets(item->id, KIOTTY_SPEC_LINK_TYPE_SATISFIES), s_.failure)) requirementIds.insert(target);
            todo.push_back(std::move(*item));
        }
    }
    b.text("### 포함 항목 상태\n" + states + "\n");
    const auto render = [this](const ItemRecord &record, bool full) { return renderItem(record, full); };
    b.text("## 구현할 항목 (미구현·변경 필요)\n");
    b.items(todo, render);
    std::vector<ItemRecord> upstream;
    for (const auto &id : requirementIds) {
        if (auto item = expect(s_.items.find(id), s_.failure)) upstream.push_back(std::move(*item));
    }
    b.text("## 관련 요구사항·인터페이스\n");
    b.items(upstream, [this](const ItemRecord &record, bool) { return renderItem(record, false); });
    b.text(rules());
    const auto decisions = expect(s_.decisions.get(KIOTTY_SPEC_DECISION_KIND_DESIGN), s_.failure);
    b.text("## 결정 기록: design\n" + (decisions.empty() ? std::string("없음\n") : decisions) + "\n");
    return b.finish();
}

std::string ContextBuilder::forChangeRequest(std::string_view crId, size_t maxBytes) {
    const auto change = expect(s_.changes.get(crId), s_.failure);
    Budget b(maxBytes);
    b.text("# 변경 요청 " + change.id + " (" + std::string(name(change.source)) + ", " +
           std::string(name(change.state)) + ", 진입 Step " + std::to_string(change.entryStep) + ")\n\n" +
           change.body + "\n\n");
    std::vector<ItemRecord> mentioned;
    for (const auto kind : {KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, KIOTTY_SPEC_ITEM_KIND_RULE, KIOTTY_SPEC_ITEM_KIND_INTERFACE,
                            KIOTTY_SPEC_ITEM_KIND_DESIGN, KIOTTY_SPEC_ITEM_KIND_UNIT_TEST, KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST,
                            KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST, KIOTTY_SPEC_ITEM_KIND_MILESTONE}) {
        for (auto &item : expect(s_.finder.ofKind(kind), s_.failure)) {
            if (change.body.find(item.id) != std::string::npos) mentioned.push_back(std::move(item));
        }
    }
    const auto render = [this](const ItemRecord &record, bool full) { return renderItem(record, full); };
    b.text("## 언급된 항목\n");
    b.items(mentioned, render);
    std::vector<ItemRecord> affected;
    std::set<std::string> seen;
    for (const auto &item : mentioned) seen.insert(std::string(item.id));
    for (const auto &item : mentioned) {
        for (const auto &id : expect(s_.graph.traverse(item.id, KIOTTY_SPEC_DIRECTION_DOWNSTREAM, 0), s_.failure)) {
            if (!seen.insert(id).second) continue;
            if (auto found = expect(s_.items.find(id), s_.failure)) affected.push_back(std::move(*found));
        }
    }
    b.text("## 하류 영향 항목 (진입 단계 판단 근거)\n");
    b.items(affected, [this](const ItemRecord &record, bool) { return renderItem(record, false); });
    return b.finish();
}

std::string ContextBuilder::rules() {
    std::string out = "## 개발 규칙\n";
    const auto list = expect(s_.finder.ofKind(KIOTTY_SPEC_ITEM_KIND_RULE), s_.failure);
    if (list.empty()) return out + "없음\n\n";
    for (const auto &rule : list) out += renderItem(rule, true);
    return out;
}

std::string ContextBuilder::pending() {
    std::string out = "# 지금 할 일\n\n";
    const auto latest = expect(s_.checkpoints.latest(), s_.failure);
    if (latest && !latest->confirmed) {
        out += "- 미확정 체크포인트 " + checkpointName(latest->seq) + " (Step " + std::to_string(latest->step) +
               ") — 사용자 확정 대기\n";
    }
    const size_t writes = expect(s_.history.pendingCount(), s_.failure);
    if (writes > 0) out += "- 아직 체크포인트에 묶이지 않은 쓰기 " + std::to_string(writes) + "건 — 라운드를 마치면 `ks checkpoint create`\n";
    for (const auto &change : expect(s_.changes.list(static_cast<KiottySpecChangeState>(0)), s_.failure)) {
        if (change.state == KIOTTY_SPEC_CHANGE_STATE_OPEN || change.state == KIOTTY_SPEC_CHANGE_STATE_IN_PROGRESS) {
            out += "- 변경 요청 " + change.id + " (" + std::string(name(change.state)) + ", 진입 Step " +
                   std::to_string(change.entryStep) + ")\n";
        }
    }
    for (const auto &item : expect(s_.results.failed(static_cast<KiottySpecItemKind>(0)), s_.failure)) {
        out += "- FAIL " + item.id + " " + item.title + "\n";
    }
    for (const auto &milestone : expect(s_.finder.ofKind(KIOTTY_SPEC_ITEM_KIND_MILESTONE), s_.failure)) {
        for (const auto &entry : expect(s_.milestones.entries(milestone.id), s_.failure)) {
            if (entry.state == KIOTTY_SPEC_IMPLEMENTATION_STATE_NEEDS_CHANGE) {
                out += "- 변경 필요 " + entry.itemId + " (" + milestone.id + ")\n";
            }
        }
    }
    return out == "# 지금 할 일\n\n" ? out + "없음\n" : out;
}

std::string ContextBuilder::status() {
    std::string out = "# 프로젝트 상태\n\n";
    const auto latest = expect(s_.checkpoints.latest(), s_.failure);
    const auto confirmed = expect(s_.checkpoints.lastConfirmed(0), s_.failure);
    out += "- 최근 체크포인트: " + (latest ? checkpointName(latest->seq) + " (Step " + std::to_string(latest->step) +
                                                 (latest->confirmed ? ", 확정" : ", 미확정") + ")"
                                           : std::string("없음")) + "\n";
    out += "- 마지막 확정: " + (confirmed ? checkpointName(confirmed->seq) + " (Step " + std::to_string(confirmed->step) + ")"
                                          : std::string("없음")) + "\n";
    out += "\n| 종류 | 개수 |\n| --- | --- |\n";
    for (const auto kind : {KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, KIOTTY_SPEC_ITEM_KIND_RULE, KIOTTY_SPEC_ITEM_KIND_INTERFACE,
                            KIOTTY_SPEC_ITEM_KIND_DESIGN, KIOTTY_SPEC_ITEM_KIND_UNIT_TEST, KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST,
                            KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST, KIOTTY_SPEC_ITEM_KIND_MILESTONE}) {
        out += "| " + std::string(label(kind)) + " | " + std::to_string(expect(s_.finder.ofKind(kind), s_.failure).size()) + " |\n";
    }
    const auto progress = expect(s_.milestones.progress(), s_.failure);
    out += "\n- 마일스톤 포함 항목: " + std::to_string(progress.total) + " (구현 완료 " + std::to_string(progress.implemented) +
           ", 변경 필요 " + std::to_string(progress.needsChange) + ")\n";
    out += "\n| 테스트 | 전체 | PASS | FAIL | 미실행 |\n| --- | --- | --- | --- | --- |\n";
    for (const auto kind : {KIOTTY_SPEC_ITEM_KIND_UNIT_TEST, KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST, KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST,
                            KIOTTY_SPEC_ITEM_KIND_RULE}) {
        const auto summary = expect(s_.results.summary(kind), s_.failure);
        out += "| " + std::string(kind == KIOTTY_SPEC_ITEM_KIND_RULE ? "코드 검사(규칙)" : label(kind)) + " | " +
               std::to_string(summary.total) + " | " + std::to_string(summary.pass) + " | " + std::to_string(summary.fail) +
               " | " + std::to_string(summary.notRun) + " |\n";
    }
    out += "\n- 열린 변경 요청: " + std::to_string(expect(s_.changes.openCount(), s_.failure)) + "\n";
    return out;
}

}
