#include "implementation/domain/usecases/journey.h"

#include "implementation/core/text_view.h"
#include "implementation/domain/entity/names.h"

#include <algorithm>

namespace kiotty::spec::implementation {

namespace {

constexpr int32_t LAST_STEP = 10;
constexpr char RELEASE_REASON[] = "RELEASE";

std::string stepName(int32_t step) { return "Step " + std::to_string(step) + " " + std::string(stepLabel(step)); }

bool isOpen(const ChangeRequestRecord &change)
{
    return change.state == KIOTTY_SPEC_CHANGE_STATE_OPEN || change.state == KIOTTY_SPEC_CHANGE_STATE_IN_PROGRESS;
}

std::string describe(const std::vector<ItemChange> &changes)
{
    std::string text;
    for (const ItemChange &change : changes) {
        const char *verb = change.added ? "추가" : "수정";
        if (change.record.removed) { verb = "삭제"; }
        text += "- " + std::string(verb) + " " + change.record.id + " " + change.record.title + "\n";
    }
    if (text.empty()) { return "없음\n"; }
    return text;
}

std::string pendingNote(const PositionRecord &position)
{
    return " / 체크포인트에 묶이지 않은 쓰기 " + std::to_string(position.pendingWrites) +
           "건 있음: 라운드를 마치면 `ks checkpoint create --step " + std::to_string(std::max(position.nextStep, 1)) + "`";
}

std::string runTable(const std::vector<StepRunRecord> &history)
{
    if (history.empty()) { return "없음\n\n"; }
    std::string out = "| # | 단계 | 체크포인트 | 라운드 | 확정 | 이 단계로 온 이유 | 마지막 요약 |\n| --- | --- | --- | --- | --- | --- | --- |\n";
    size_t index = 0;
    for (const StepRunRecord &run : history) {
        std::string range = checkpointName(run.firstSeq);
        if (run.firstSeq != run.lastSeq) { range += " ~ " + checkpointName(run.lastSeq); }
        const std::string confirmed = run.confirmedSeq ? checkpointName(*run.confirmedSeq) : "-";
        out += "| " + std::to_string(++index) + " | " + stepName(run.step) + " | " + range + " | " + std::to_string(run.count) +
               " | " + confirmed + " | " + run.reason + " | " + run.lastNote + " |\n";
    }
    return out + "\n";
}

void truncateUtf8(std::string &out, size_t maxBytes)
{
    if (maxBytes == 0 || out.size() <= maxBytes) { return; }
    out.resize(maxBytes);
    while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0U) == 0x80U) { out.pop_back(); }
    if (!out.empty() && (static_cast<unsigned char>(out.back()) & 0x80U) != 0) { out.pop_back(); }
    out += "\n> (한도 초과로 잘림)\n";
}

}

PositionResult Journey::position()
{
    PositionRecord position;
    const auto pending = _s.history.pendingCount();
    if (!pending) { return PositionResult::failure(pending.code); }
    position.pendingWrites = pending.value;
    const auto latest = _s.checkpoints.latest();
    if (!latest) { return PositionResult::failure(latest.code); }
    const auto open = openChanges();
    if (!open) { return PositionResult::failure(open.code); }
    std::string action = "아직 체크포인트가 없음. Step 1 요구사항 도출부터 시작: `ks context step 1`";
    position.nextStep = 1;
    if (latest.value) {
        position.checkpointSeq = latest.value->seq;
        position.step = latest.value->step;
        position.confirmed = latest.value->confirmed;
        const auto all = _s.checkpoints.list(0);
        if (!all) { return PositionResult::failure(all.code); }
        for (const CheckpointRecord &record : all.value) {
            if (record.purpose == KIOTTY_SPEC_CHECKPOINT_PURPOSE_RELEASE && record.confirmed) { position.released = true; }
        }
        const auto move = nextMove(*latest.value, open.value);
        if (!move) { return PositionResult::failure(move.code); }
        position.nextStep = move.value.step;
        action = move.value.action;
    }
    if (position.pendingWrites > 0) { action += pendingNote(position); }
    const KiottySpecStatus assigned = assign(position.nextAction, action);
    if (assigned != KIOTTY_SPEC_STATUS_SUCCESS) { return PositionResult::failure(assigned); }
    return PositionResult::success(position);
}

StepRunListResult Journey::runs()
{
    const auto checkpoints = _s.checkpoints.list(0);
    if (!checkpoints) { return StepRunListResult::failure(checkpoints.code); }
    const auto changes = _s.changes.list(static_cast<KiottySpecChangeState>(0));
    if (!changes) { return StepRunListResult::failure(changes.code); }
    std::vector<StepRunRecord> runs;
    for (const CheckpointRecord &cp : checkpoints.value) {
        const bool release = cp.purpose == KIOTTY_SPEC_CHECKPOINT_PURPOSE_RELEASE;
        const bool sameRun = !runs.empty() && runs.back().step == cp.step && !release && runs.back().reason != RELEASE_REASON;
        if (!sameRun) {
            StepRunRecord run;
            run.step = cp.step;
            run.firstSeq = cp.seq;
            const std::string reason = release ? std::string(RELEASE_REASON) : reasonOf(runs, cp, changes.value);
            const KiottySpecStatus assigned = assign(run.reason, reason);
            if (assigned != KIOTTY_SPEC_STATUS_SUCCESS) { return StepRunListResult::failure(assigned); }
            runs.push_back(run);
        }
        StepRunRecord &run = runs.back();
        run.lastSeq = cp.seq;
        ++run.count;
        run.lastNote = cp.note;
        if (cp.confirmed) { run.confirmedSeq = cp.seq; }
    }
    return StepRunListResult::success(std::move(runs));
}

TextResult Journey::whereAmI(size_t maxBytes)
{
    const auto position = this->position();
    if (!position) { return TextResult::failure(position.code); }
    const PositionRecord &where = position.value;
    std::string out = "# 현재 위치\n\n";
    const auto projectName = _s.meta.get("project.name");
    if (!projectName) { return TextResult::failure(projectName.code); }
    if (projectName.value) { out += "- 프로젝트: " + *projectName.value + "\n"; }
    if (where.checkpointSeq) {
        out += "- 체크포인트: " + checkpointName(*where.checkpointSeq) + " (" + stepName(where.step) + ", " +
               (where.confirmed ? "확정" : "미확정") + ")\n";
    } else {
        out += "- 체크포인트: 없음\n";
    }
    if (where.released) { out += "- RELEASE 이후 (유지보수)\n"; }
    out += "- **다음 할 일**: " + where.nextAction + "\n\n";

    const auto history = runs();
    if (!history) { return TextResult::failure(history.code); }
    out += "## 지나온 단계\n\n" + runTable(history.value);

    const auto recent = recentChanges(where);
    if (!recent) { return recent; }
    out += recent.value;

    const auto open = openChanges();
    if (!open) { return TextResult::failure(open.code); }
    std::string openText;
    for (const ChangeRequestRecord &change : open.value) {
        openText += "- " + change.id + " (" + std::string(name(change.state)) + ", 진입 Step " + std::to_string(change.entryStep) + ") " +
                    change.body + "\n";
    }
    if (!openText.empty()) { out += "## 열린 변경 요청\n" + openText + "\n"; }

    if (where.step >= 1) {
        const auto verdict = _s.checker.confirmCheck(where.step);
        if (!verdict) { return TextResult::failure(verdict.code); }
        out += "## " + stepName(where.step) + " 확정 조건\n" + verdict.value.report + "\n";
    }
    out += "> 단계 입력은 `ks context step " + std::to_string(std::max(where.nextStep, 1)) + "`, 할 일 목록은 `ks context pending`.\n";
    truncateUtf8(out, maxBytes);
    return TextResult::success(std::move(out));
}

NextMoveResult Journey::nextMove(const CheckpointRecord &latest, const std::vector<ChangeRequestRecord> &open)
{
    const std::string cp = checkpointName(latest.seq);
    const auto earliestOpen = std::min_element(open.begin(), open.end(), [](const ChangeRequestRecord &a, const ChangeRequestRecord &b) {
        return a.entryStep < b.entryStep;
    });
    NextMove move;
    if (!latest.confirmed) {
        const auto verdict = _s.checker.confirmCheck(latest.step, latest.seq);
        if (!verdict) { return NextMoveResult::failure(verdict.code); }
        const std::string unmet = " 단, 확정 조건 미충족: `ks checkpoint confirm-check " + std::to_string(latest.step) + "`.";
        move.step = latest.step;
        move.action = cp + " 사용자 검토·확정 대기 (`ks checkpoint confirm " + cp + " --actor user`)." +
                      (verdict.value.ok ? std::string() : unmet) + " 피드백이 있으면 같은 Step 에서 한 라운드 더 진행";
        return NextMoveResult::success(std::move(move));
    }
    if (earliestOpen != open.end()) {
        move.step = earliestOpen->entryStep;
        move.action = "열린 변경 요청 " + earliestOpen->id + " 반영: " + stepName(earliestOpen->entryStep) + " 부터 다시 진행 (`ks context cr " +
                      earliestOpen->id + "`)";
        return NextMoveResult::success(std::move(move));
    }
    if (latest.purpose == KIOTTY_SPEC_CHECKPOINT_PURPOSE_RELEASE) {
        move.step = latest.step;
        move.action = "RELEASE 완료. 이슈가 생기면 변경 요청으로 등록: `ks cr create --source issue ...`";
        return NextMoveResult::success(std::move(move));
    }
    if (latest.step < LAST_STEP) {
        move.step = latest.step + 1;
        move.action = stepName(latest.step + 1) + " 시작: `ks context step " + std::to_string(latest.step + 1) + "`";
        return NextMoveResult::success(std::move(move));
    }
    const auto verdict = _s.checker.releaseCheck();
    if (!verdict) { return NextMoveResult::failure(verdict.code); }
    move.step = LAST_STEP;
    move.action = verdict.value.ok ? "Step 10 확정됨. RELEASE 가능: `ks checkpoint release --actor user`"
                                   : "Step 10 확정됨. RELEASE 조건 미충족: `ks checkpoint release-check`";
    return NextMoveResult::success(std::move(move));
}

std::string Journey::reasonOf(const std::vector<StepRunRecord> &runs, const CheckpointRecord &cp, const std::vector<ChangeRequestRecord> &changes)
{
    if (runs.empty()) { return "시작"; }
    const StepRunRecord &previous = runs.back();
    if (cp.step > previous.step) {
        if (cp.step == previous.step + 1) { return "다음 단계"; }
        return "Step " + std::to_string(previous.step) + " 에서 건너뜀";
    }
    std::string because;
    for (const ChangeRequestRecord &change : changes) {
        const bool openedBetween = change.cpOpened > previous.lastSeq && change.cpOpened <= cp.seq;
        if (!openedBetween) { continue; }
        if (!because.empty()) { because += ", "; }
        because += std::string(change.id);
    }
    if (because.empty()) { return "되돌아옴"; }
    return because + " 로 되돌아옴";
}

TextResult Journey::recentChanges(const PositionRecord &position)
{
    if (!position.checkpointSeq) { return TextResult::success(std::string()); }
    const auto checkpoints = _s.checkpoints.list(0);
    if (!checkpoints) { return TextResult::failure(checkpoints.code); }
    const int64_t latest = *position.checkpointSeq;
    int64_t previous = 0;
    for (const CheckpointRecord &cp : checkpoints.value) {
        if (cp.seq < latest) { previous = cp.seq; }
    }
    const auto round = _s.finder.changedBetween(previous, latest);
    if (!round) { return TextResult::failure(round.code); }
    std::string out = "## 마지막 라운드(" + checkpointName(latest) + ")에서 바뀐 항목\n" + describe(round.value) + "\n";
    if (position.pendingWrites == 0) { return TextResult::success(std::move(out)); }
    const auto pending = _s.finder.changedBetween(latest, kCurrent);
    if (!pending) { return TextResult::failure(pending.code); }
    out += "## 아직 체크포인트에 묶이지 않은 변경\n" + describe(pending.value) + "\n";
    return TextResult::success(std::move(out));
}

ChangeRequestListResult Journey::openChanges()
{
    auto changes = _s.changes.list(static_cast<KiottySpecChangeState>(0));
    if (!changes) { return changes; }
    std::vector<ChangeRequestRecord> open;
    for (ChangeRequestRecord &change : changes.value) {
        if (isOpen(change)) { open.push_back(std::move(change)); }
    }
    return ChangeRequestListResult::success(std::move(open));
}

KiottySpecStatus Journey::assign(NoteText &target, const std::string &text)
{
    if (target.assign(spanOf(text)) == KIOTTY_SPEC_STATUS_SUCCESS) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    return _s.failure.raise(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, {literalText("문구가 너무 김 (최대 512 바이트): "), spanOf(text)});
}

}
