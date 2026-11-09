#include "implementation/context/context_builder.h"

#include "implementation/core/text.h"
#include "implementation/core/text_view.h"
#include "implementation/domain/entity/names.h"

#include <set>
#include <utility>

namespace kiotty::spec::implementation {

namespace {

constexpr size_t DEFAULT_BUDGET = 60000;
constexpr int32_t DEFAULT_DEPTH = 2;
constexpr char EMPTY_HEADING[] = "# 지금 할 일\n\n";

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
    if (files.empty()) { return "없음\n\n"; }
    std::string out;
    for (const auto &file : files) { out += "- " + file.id + " [" + std::string(name(file.role)) + "] " + file.path + "\n"; }
    return out + "\n";
}

const char *changeVerb(const ItemChange &change) {
    if (change.record.removed) { return " (삭제)"; }
    return change.added ? " (추가)" : " (수정)";
}

bool isOpen(KiottySpecChangeState state) {
    return state == KIOTTY_SPEC_CHANGE_STATE_OPEN || state == KIOTTY_SPEC_CHANGE_STATE_IN_PROGRESS;
}

}

class Budget {
public:
    Budget(size_t maxBytes, LinkStore &links) : _max(maxBytes == 0 ? DEFAULT_BUDGET : maxBytes), _links(links) {}

    void text(const std::string &value) {
        if (_out.size() + value.size() <= _max) { _out += value; }
        else { ++_omitted; }
    }

    KiottySpecStatus items(const std::vector<ItemRecord> &list, bool allowFull) {
        if (list.empty()) {
            text("없음\n\n");
            return KIOTTY_SPEC_STATUS_SUCCESS;
        }
        for (const auto &item : list) {
            const KiottySpecStatus added = add(item, allowFull);
            if (added != KIOTTY_SPEC_STATUS_SUCCESS) { return added; }
        }
        _out += "\n";
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    std::string finish() {
        if (_omitted > 0) {
            _out += "\n> 한도 초과로 " + std::to_string(_omitted) + "개 조각 생략. `ks context item <ID>` 로 개별 조회할 것.\n";
        }
        return std::move(_out);
    }

private:
    KiottySpecStatus add(const ItemRecord &item, bool allowFull) {
        const auto full = renderItem(_links, item, allowFull);
        if (!full) { return full.code; }
        if (_out.size() + full.value.size() <= _max * 85 / 100) {
            _out += full.value;
            return KIOTTY_SPEC_STATUS_SUCCESS;
        }
        const auto brief = renderItem(_links, item, false);
        if (!brief) { return brief.code; }
        if (_out.size() + brief.value.size() <= _max) { _out += brief.value; }
        else { ++_omitted; }
        return KIOTTY_SPEC_STATUS_SUCCESS;
    }

    size_t _max;
    LinkStore &_links;
    size_t _omitted = 0;
    std::string _out;
};

TextResult renderItem(LinkStore &links, const ItemRecord &item, bool full) {
    std::string out = "### " + item.id;
    const auto category = label(item.category);
    if (!category.empty()) { out += " [" + std::string(category) + "]"; }
    out += " " + item.title + "\n";
    const auto upstream = links.list(item.id, KIOTTY_SPEC_DIRECTION_UPSTREAM, static_cast<KiottySpecLinkType>(0));
    if (!upstream) { return TextResult::failure(upstream.code); }
    std::string joined;
    for (const auto &link : upstream.value) { joined += (joined.empty() ? "" : ", ") + std::string(name(link.type)) + " " + link.toId; }
    if (!joined.empty()) { out += "> " + joined + "\n"; }
    if (!item.parentId.empty()) { out += "> parent " + item.parentId + "\n"; }
    if (full && !item.body.empty()) { out += "\n" + item.body + "\n"; }
    return TextResult::success(out + "\n");
}

KiottySpecStatus ContextBuilder::section(Budget &budget, std::string_view title, const ItemListResult &items) {
    if (!items) { return items.code; }
    budget.text("## " + std::string(title) + " (" + std::to_string(items.value.size()) + ")\n");
    return budget.items(items.value, true);
}

KiottySpecStatus ContextBuilder::kindSection(Budget &budget, std::string_view title, KiottySpecItemKind kind) {
    return section(budget, title, _s.finder.ofKind(kind));
}

KiottySpecStatus ContextBuilder::decision(Budget &budget, KiottySpecDecisionKind kind) {
    const auto body = _s.decisions.get(kind);
    if (!body) { return body.code; }
    budget.text("## 결정 기록: " + std::string(name(kind)) + "\n" + (body.value.empty() ? "없음\n" : body.value) + "\n\n");
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus ContextBuilder::fileSection(Budget &budget, std::string_view title, std::initializer_list<KiottySpecFileRole> roles) {
    std::string text = "## " + std::string(title) + "\n";
    for (const KiottySpecFileRole role : roles) {
        const auto files = _s.files.list(role);
        if (!files) { return files.code; }
        text += fileList(files.value);
    }
    budget.text(text);
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus ContextBuilder::stepChanges(Budget &budget) {
    const auto confirmed = _s.checkpoints.lastConfirmed(0);
    if (!confirmed) { return confirmed.code; }
    if (confirmed.value) {
        const auto changed = _s.finder.changedBetween(confirmed.value->seq, kCurrent);
        if (!changed) { return changed.code; }
        if (!changed.value.empty()) {
            std::string list;
            for (const auto &change : changed.value) { list += "- " + change.record.id + changeVerb(change) + " " + change.record.title + "\n"; }
            budget.text("## " + checkpointName(confirmed.value->seq) + " 이후 바뀐 항목\n" + list + "\n");
            const KiottySpecStatus impact = section(budget, "바뀐 항목과 링크된 영향 항목 (다시 볼 것)", _s.graph.impactSince(confirmed.value->seq));
            if (impact != KIOTTY_SPEC_STATUS_SUCCESS) { return impact; }
        }
    }
    const auto open = _s.changes.list(KIOTTY_SPEC_CHANGE_STATE_OPEN);
    if (!open) { return open.code; }
    if (!open.value.empty()) {
        std::string list;
        for (const auto &change : open.value) {
            list += "- " + change.id + " (진입 Step " + std::to_string(change.entryStep) + ") " + change.body + "\n";
        }
        budget.text("## 열린 변경 요청\n" + list + "\n");
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus ContextBuilder::stepSections(Budget &budget, int32_t step, size_t maxBytes) {
    KiottySpecStatus status = KIOTTY_SPEC_STATUS_SUCCESS;
    const auto next = [&status](KiottySpecStatus result) {
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = result; }
        return status == KIOTTY_SPEC_STATUS_SUCCESS;
    };
    const auto basics = [&]() {
        return next(kindSection(budget, "요구사항", KIOTTY_SPEC_ITEM_KIND_REQUIREMENT)) &&
               next(kindSection(budget, "개발 규칙", KIOTTY_SPEC_ITEM_KIND_RULE));
    };
    switch (step) {
    case 1:
        basics();
        break;
    case 2:
        basics() && next(decision(budget, KIOTTY_SPEC_DECISION_KIND_REQUIREMENT)) && next(decision(budget, KIOTTY_SPEC_DECISION_KIND_RULE));
        break;
    case 3:
        basics() && next(kindSection(budget, "외부 인터페이스 항목", KIOTTY_SPEC_ITEM_KIND_INTERFACE)) &&
            next(fileSection(budget, "인터페이스 파일", {KIOTTY_SPEC_FILE_ROLE_GUI_DRAFT, KIOTTY_SPEC_FILE_ROLE_C_HEADER})) &&
            next(decision(budget, KIOTTY_SPEC_DECISION_KIND_INTERFACE));
        break;
    case 4:
        basics() && next(kindSection(budget, "외부 인터페이스 항목", KIOTTY_SPEC_ITEM_KIND_INTERFACE)) &&
            next(kindSection(budget, "설계 항목", KIOTTY_SPEC_ITEM_KIND_DESIGN)) && next(decision(budget, KIOTTY_SPEC_DECISION_KIND_DESIGN));
        break;
    case 5: {
        ItemQuery testRules;
        testRules.kind = KIOTTY_SPEC_ITEM_KIND_RULE;
        testRules.category = KIOTTY_SPEC_ITEM_CATEGORY_RULE_TEST;
        next(kindSection(budget, "설계 항목", KIOTTY_SPEC_ITEM_KIND_DESIGN)) && next(section(budget, "테스트 규칙", _s.items.list(testRules))) &&
            next(kindSection(budget, "유닛 테스트", KIOTTY_SPEC_ITEM_KIND_UNIT_TEST)) && next(decision(budget, KIOTTY_SPEC_DECISION_KIND_UNIT_TEST));
        break;
    }
    case 6:
        next(kindSection(budget, "설계 항목", KIOTTY_SPEC_ITEM_KIND_DESIGN)) && next(kindSection(budget, "유닛 테스트", KIOTTY_SPEC_ITEM_KIND_UNIT_TEST)) &&
            next(kindSection(budget, "개발 규칙", KIOTTY_SPEC_ITEM_KIND_RULE)) && next(kindSection(budget, "마일스톤", KIOTTY_SPEC_ITEM_KIND_MILESTONE)) &&
            next(section(budget, "아직 어떤 마일스톤에도 포함되지 않은 항목", _s.milestones.uncovered())) &&
            next(decision(budget, KIOTTY_SPEC_DECISION_KIND_PLAN));
        break;
    case 7: {
        const auto milestone = _s.milestones.next();
        if (!milestone) { return milestone.code; }
        if (milestone.value) {
            const auto text = forMilestone(*milestone.value, maxBytes / 2);
            if (!text) { return text.code; }
            budget.text(text.value);
        } else {
            budget.text("## 다음 마일스톤\n남은 마일스톤 없음\n\n");
        }
        next(fileSection(budget, "소스 파일", {KIOTTY_SPEC_FILE_ROLE_SOURCE}));
        break;
    }
    case 8: {
        ItemQuery functional;
        functional.kind = KIOTTY_SPEC_ITEM_KIND_REQUIREMENT;
        functional.category = KIOTTY_SPEC_ITEM_CATEGORY_REQUIREMENT_FUNCTIONAL;
        if (!(next(section(budget, "기능 요구사항", _s.items.list(functional))) &&
              next(kindSection(budget, "시스템 테스트", KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST)))) { break; }
        const auto verdict = _s.checker.confirmCheck(8);
        if (!verdict) { return verdict.code; }
        budget.text("## 1:1 검사\n" + verdict.value.report + "\n");
        next(fileSection(budget, "소스 파일", {KIOTTY_SPEC_FILE_ROLE_SOURCE}));
        break;
    }
    case 9:
        next(kindSection(budget, "개발 규칙", KIOTTY_SPEC_ITEM_KIND_RULE)) && next(fileSection(budget, "소스 파일", {KIOTTY_SPEC_FILE_ROLE_SOURCE}));
        break;
    case 10: {
        if (!(next(kindSection(budget, "외부 인터페이스 항목", KIOTTY_SPEC_ITEM_KIND_INTERFACE)) &&
              next(kindSection(budget, "E2E 테스트", KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST)))) { break; }
        const auto verdict = _s.checker.confirmCheck(10);
        if (!verdict) { return verdict.code; }
        budget.text("## 1:1 검사\n" + verdict.value.report + "\n");
        next(fileSection(budget, "인터페이스 파일", {KIOTTY_SPEC_FILE_ROLE_GUI_DRAFT, KIOTTY_SPEC_FILE_ROLE_C_HEADER})) &&
            next(fileSection(budget, "소스 파일", {KIOTTY_SPEC_FILE_ROLE_SOURCE}));
        break;
    }
    default:
        break;
    }
    return status;
}

TextResult ContextBuilder::forStep(int32_t step, size_t maxBytes) {
    if (step < 1 || step > 10) {
        return TextResult::failure(_s.failure.raise(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("step 은 1 ~ 10 이어야 함")}));
    }
    Budget budget(maxBytes, _s.links);
    budget.text("# Step " + std::to_string(step) + " " + std::string(stepLabel(step)) + "\n\n목표: " + stepGoal(step) + "\n\n");
    const KiottySpecStatus changes = stepChanges(budget);
    if (changes != KIOTTY_SPEC_STATUS_SUCCESS) { return TextResult::failure(changes); }
    const KiottySpecStatus sections = stepSections(budget, step, maxBytes);
    if (sections != KIOTTY_SPEC_STATUS_SUCCESS) { return TextResult::failure(sections); }
    return TextResult::success(budget.finish());
}

KiottySpecStatus ContextBuilder::related(Budget &budget, const ItemRecord &item, int32_t depth) {
    std::string files;
    for (const auto dir : {KIOTTY_SPEC_DIRECTION_UPSTREAM, KIOTTY_SPEC_DIRECTION_DOWNSTREAM}) {
        const auto ids = _s.graph.traverse(item.id, dir, depth <= 0 ? DEFAULT_DEPTH : depth);
        if (!ids) { return ids.code; }
        std::vector<ItemRecord> records;
        for (const auto &id : ids.value) {
            const auto found = _s.items.find(id);
            if (!found) { return found.code; }
            if (found.value) {
                records.push_back(*found.value);
                continue;
            }
            const auto file = _s.files.find(id);
            if (!file) { return file.code; }
            if (file.value) { files += "- " + file.value->id + " " + file.value->path + "\n"; }
        }
        budget.text(dir == KIOTTY_SPEC_DIRECTION_UPSTREAM ? "## 상류 (이 항목이 만족·검증·구현하는 것)\n" : "## 하류 (이 항목을 만족·검증·구현하는 것)\n");
        const KiottySpecStatus added = budget.items(records, true);
        if (added != KIOTTY_SPEC_STATUS_SUCCESS) { return added; }
    }
    if (!files.empty()) { budget.text("## 연결된 파일\n" + files + "\n"); }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

TextResult ContextBuilder::forItem(std::string_view itemId, int32_t depth, size_t maxBytes) {
    const auto item = _s.items.get(itemId);
    if (!item) { return TextResult::failure(item.code); }
    const auto rendered = renderItem(_s.links, item.value, true);
    if (!rendered) { return rendered; }
    Budget budget(maxBytes, _s.links);
    budget.text("# " + item.value.id + " 맥락\n\n" + rendered.value);
    const KiottySpecStatus linked = related(budget, item.value, depth);
    if (linked != KIOTTY_SPEC_STATUS_SUCCESS) { return TextResult::failure(linked); }

    std::string lines;
    for (const auto kind : {KIOTTY_SPEC_DECISION_KIND_REQUIREMENT, KIOTTY_SPEC_DECISION_KIND_RULE, KIOTTY_SPEC_DECISION_KIND_INTERFACE,
                            KIOTTY_SPEC_DECISION_KIND_DESIGN, KIOTTY_SPEC_DECISION_KIND_UNIT_TEST, KIOTTY_SPEC_DECISION_KIND_PLAN}) {
        const auto body = _s.decisions.get(kind);
        if (!body) { return body; }
        for (const auto line : splitLines(body.value)) {
            if (line.find(viewOf(item.value.id.view())) != std::string_view::npos) { lines += std::string(line) + "\n"; }
        }
    }
    if (!lines.empty()) { budget.text("## 관련 결정\n" + lines + "\n"); }
    const auto result = _s.results.last(item.value.id);
    if (!result) { return TextResult::failure(result.code); }
    if (result.value) { budget.text("## 마지막 결과\n" + std::string(name(result.value->result)) + "\n" + result.value->log + "\n"); }
    return TextResult::success(budget.finish());
}

TextResult ContextBuilder::forMilestone(std::string_view milestoneId, size_t maxBytes) {
    const auto milestone = _s.items.get(milestoneId);
    if (!milestone) { return TextResult::failure(milestone.code); }
    Budget budget(maxBytes, _s.links);
    budget.text("## 마일스톤 " + milestone.value.id + " " + milestone.value.title + "\n\n" + milestone.value.body + "\n\n");
    const auto entries = _s.milestones.entries(milestone.value.id);
    if (!entries) { return TextResult::failure(entries.code); }
    std::vector<ItemRecord> todo;
    std::string states;
    std::set<std::string> requirementIds;
    for (const auto &entry : entries.value) {
        states += "- " + entry.itemId + " : " + std::string(name(entry.state)) + "\n";
        if (entry.state == KIOTTY_SPEC_IMPLEMENTATION_STATE_IMPLEMENTED) { continue; }
        const auto item = _s.items.find(entry.itemId);
        if (!item) { return TextResult::failure(item.code); }
        if (!item.value) { continue; }
        const auto targets = _s.links.targets(item.value->id, KIOTTY_SPEC_LINK_TYPE_SATISFIES);
        if (!targets) { return TextResult::failure(targets.code); }
        requirementIds.insert(targets.value.begin(), targets.value.end());
        todo.push_back(*item.value);
    }
    budget.text("### 포함 항목 상태\n" + states + "\n");
    budget.text("## 구현할 항목 (미구현·변경 필요)\n");
    KiottySpecStatus added = budget.items(todo, true);
    if (added != KIOTTY_SPEC_STATUS_SUCCESS) { return TextResult::failure(added); }
    std::vector<ItemRecord> upstream;
    for (const auto &id : requirementIds) {
        const auto item = _s.items.find(id);
        if (!item) { return TextResult::failure(item.code); }
        if (item.value) { upstream.push_back(*item.value); }
    }
    budget.text("## 관련 요구사항·인터페이스\n");
    added = budget.items(upstream, false);
    if (added != KIOTTY_SPEC_STATUS_SUCCESS) { return TextResult::failure(added); }
    const auto ruleText = rules();
    if (!ruleText) { return ruleText; }
    budget.text(ruleText.value);
    const auto design = _s.decisions.get(KIOTTY_SPEC_DECISION_KIND_DESIGN);
    if (!design) { return design; }
    budget.text("## 결정 기록: design\n" + (design.value.empty() ? std::string("없음\n") : design.value) + "\n");
    return TextResult::success(budget.finish());
}

TextResult ContextBuilder::forChangeRequest(std::string_view crId, size_t maxBytes) {
    const auto change = _s.changes.get(crId);
    if (!change) { return TextResult::failure(change.code); }
    const ChangeRequestRecord &record = change.value;
    Budget budget(maxBytes, _s.links);
    budget.text("# 변경 요청 " + record.id + " (" + std::string(name(record.source)) + ", " + std::string(name(record.state)) +
                ", 진입 Step " + std::to_string(record.entryStep) + ")\n\n" + record.body + "\n\n");
    std::vector<ItemRecord> mentioned;
    for (const auto kind : {KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, KIOTTY_SPEC_ITEM_KIND_RULE, KIOTTY_SPEC_ITEM_KIND_INTERFACE,
                            KIOTTY_SPEC_ITEM_KIND_DESIGN, KIOTTY_SPEC_ITEM_KIND_UNIT_TEST, KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST,
                            KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST, KIOTTY_SPEC_ITEM_KIND_MILESTONE}) {
        const auto items = _s.finder.ofKind(kind);
        if (!items) { return TextResult::failure(items.code); }
        for (const auto &item : items.value) {
            if (record.body.find(viewOf(item.id.view())) != std::string::npos) { mentioned.push_back(item); }
        }
    }
    budget.text("## 언급된 항목\n");
    KiottySpecStatus added = budget.items(mentioned, true);
    if (added != KIOTTY_SPEC_STATUS_SUCCESS) { return TextResult::failure(added); }
    std::vector<ItemRecord> affected;
    std::set<std::string> seen;
    for (const auto &item : mentioned) { seen.insert(std::string(item.id)); }
    for (const auto &item : mentioned) {
        const auto ids = _s.graph.traverse(item.id, KIOTTY_SPEC_DIRECTION_DOWNSTREAM, 0);
        if (!ids) { return TextResult::failure(ids.code); }
        for (const auto &id : ids.value) {
            if (!seen.insert(id).second) { continue; }
            const auto found = _s.items.find(id);
            if (!found) { return TextResult::failure(found.code); }
            if (found.value) { affected.push_back(*found.value); }
        }
    }
    budget.text("## 하류 영향 항목 (진입 단계 판단 근거)\n");
    added = budget.items(affected, false);
    if (added != KIOTTY_SPEC_STATUS_SUCCESS) { return TextResult::failure(added); }
    return TextResult::success(budget.finish());
}

TextResult ContextBuilder::rules() {
    std::string out = "## 개발 규칙\n";
    const auto list = _s.finder.ofKind(KIOTTY_SPEC_ITEM_KIND_RULE);
    if (!list) { return TextResult::failure(list.code); }
    if (list.value.empty()) { return TextResult::success(out + "없음\n\n"); }
    for (const auto &rule : list.value) {
        const auto rendered = renderItem(_s.links, rule, true);
        if (!rendered) { return rendered; }
        out += rendered.value;
    }
    return TextResult::success(out);
}

KiottySpecStatus StatusBoard::pendingChanges(std::string &out) {
    const auto changes = _s.changes.list(static_cast<KiottySpecChangeState>(0));
    if (!changes) { return changes.code; }
    for (const auto &change : changes.value) {
        if (!isOpen(change.state)) { continue; }
        out += "- 변경 요청 " + change.id + " (" + std::string(name(change.state)) + ", 진입 Step " + std::to_string(change.entryStep) + ")\n";
    }
    const auto failed = _s.results.failed(static_cast<KiottySpecItemKind>(0));
    if (!failed) { return failed.code; }
    for (const auto &item : failed.value) { out += "- FAIL " + item.id + " " + item.title + "\n"; }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus StatusBoard::pendingMilestones(std::string &out) {
    const auto milestones = _s.finder.ofKind(KIOTTY_SPEC_ITEM_KIND_MILESTONE);
    if (!milestones) { return milestones.code; }
    for (const auto &milestone : milestones.value) {
        const auto entries = _s.milestones.entries(milestone.id);
        if (!entries) { return entries.code; }
        for (const auto &entry : entries.value) {
            if (entry.state == KIOTTY_SPEC_IMPLEMENTATION_STATE_NEEDS_CHANGE) { out += "- 변경 필요 " + entry.itemId + " (" + milestone.id + ")\n"; }
        }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

TextResult StatusBoard::pending() {
    std::string out = EMPTY_HEADING;
    const auto latest = _s.checkpoints.latest();
    if (!latest) { return TextResult::failure(latest.code); }
    if (latest.value && !latest.value->confirmed) {
        out += "- 미확정 체크포인트 " + checkpointName(latest.value->seq) + " (Step " + std::to_string(latest.value->step) + ") — 사용자 확정 대기\n";
    }
    const auto writes = _s.history.pendingCount();
    if (!writes) { return TextResult::failure(writes.code); }
    if (writes.value > 0) { out += "- 아직 체크포인트에 묶이지 않은 쓰기 " + std::to_string(writes.value) + "건 — 라운드를 마치면 `ks checkpoint create`\n"; }
    KiottySpecStatus status = pendingChanges(out);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return TextResult::failure(status); }
    status = pendingMilestones(out);
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return TextResult::failure(status); }
    return TextResult::success(out == EMPTY_HEADING ? out + "없음\n" : out);
}

KiottySpecStatus StatusBoard::testTable(std::string &out) {
    out += "\n| 테스트 | 전체 | PASS | FAIL | 미실행 |\n| --- | --- | --- | --- | --- |\n";
    for (const auto kind : {KIOTTY_SPEC_ITEM_KIND_UNIT_TEST, KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST, KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST,
                            KIOTTY_SPEC_ITEM_KIND_RULE}) {
        const auto summary = _s.results.summary(kind);
        if (!summary) { return summary.code; }
        out += "| " + std::string(kind == KIOTTY_SPEC_ITEM_KIND_RULE ? "코드 검사(규칙)" : label(kind)) + " | " + std::to_string(summary.value.total) +
               " | " + std::to_string(summary.value.pass) + " | " + std::to_string(summary.value.fail) + " | " + std::to_string(summary.value.notRun) + " |\n";
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

TextResult StatusBoard::status() {
    std::string out = "# 프로젝트 상태\n\n";
    const auto latest = _s.checkpoints.latest();
    if (!latest) { return TextResult::failure(latest.code); }
    const auto confirmed = _s.checkpoints.lastConfirmed(0);
    if (!confirmed) { return TextResult::failure(confirmed.code); }
    std::string latestText = "없음";
    if (latest.value) {
        latestText = checkpointName(latest.value->seq) + " (Step " + std::to_string(latest.value->step) + (latest.value->confirmed ? ", 확정" : ", 미확정") + ")";
    }
    std::string confirmedText = "없음";
    if (confirmed.value) { confirmedText = checkpointName(confirmed.value->seq) + " (Step " + std::to_string(confirmed.value->step) + ")"; }
    out += "- 최근 체크포인트: " + latestText + "\n- 마지막 확정: " + confirmedText + "\n";
    out += "\n| 종류 | 개수 |\n| --- | --- |\n";
    for (const auto kind : {KIOTTY_SPEC_ITEM_KIND_REQUIREMENT, KIOTTY_SPEC_ITEM_KIND_RULE, KIOTTY_SPEC_ITEM_KIND_INTERFACE,
                            KIOTTY_SPEC_ITEM_KIND_DESIGN, KIOTTY_SPEC_ITEM_KIND_UNIT_TEST, KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST,
                            KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST, KIOTTY_SPEC_ITEM_KIND_MILESTONE}) {
        const auto items = _s.finder.ofKind(kind);
        if (!items) { return TextResult::failure(items.code); }
        out += "| " + std::string(label(kind)) + " | " + std::to_string(items.value.size()) + " |\n";
    }
    const auto progress = _s.milestones.progress();
    if (!progress) { return TextResult::failure(progress.code); }
    out += "\n- 마일스톤 포함 항목: " + std::to_string(progress.value.total) + " (구현 완료 " + std::to_string(progress.value.implemented) +
           ", 변경 필요 " + std::to_string(progress.value.needsChange) + ")\n";
    const KiottySpecStatus table = testTable(out);
    if (table != KIOTTY_SPEC_STATUS_SUCCESS) { return TextResult::failure(table); }
    const auto open = _s.changes.openCount();
    if (!open) { return TextResult::failure(open.code); }
    out += "\n- 열린 변경 요청: " + std::to_string(open.value) + "\n";
    return TextResult::success(out);
}

}
