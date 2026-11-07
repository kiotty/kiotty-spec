#include "implementation/domain/usecases/journey.h"

#include "implementation/core/error.h"
#include "implementation/domain/entity/names.h"

#include <algorithm>

namespace kiotty::spec::implementation {

namespace {

std::string stepName(int32_t step) {
    return "Step " + std::to_string(step) + " " + std::string(stepLabel(step));
}

}

PositionRecord Journey::position() {
    PositionRecord position;
    position.pendingWrites = expect(s_.checkpoints.pendingWriteCount(), s_.failure);
    const auto latest = expect(s_.checkpoints.latest(), s_.failure);

    std::vector<ChangeRequestRecord> open;
    for (auto &change : expect(s_.changes.list(static_cast<KiottySpecChangeState>(0)), s_.failure)) {
        if (change.state == KIOTTY_SPEC_CHANGE_STATE_OPEN || change.state == KIOTTY_SPEC_CHANGE_STATE_IN_PROGRESS) {
            open.push_back(std::move(change));
        }
    }
    const auto earliestOpen = std::min_element(open.begin(), open.end(), [](const auto &a, const auto &b) {
        return a.entryStep < b.entryStep;
    });

    if (!latest) {
        position.nextStep = 1;
        position.nextAction = "아직 체크포인트가 없음. Step 1 요구사항 도출부터 시작: `ks context step 1`";
    } else {
        position.checkpointSeq = latest->seq;
        position.step = latest->step;
        position.confirmed = latest->confirmed;
        for (const auto &record : expect(s_.checkpoints.list(0), s_.failure)) {
            if (record.purpose == KIOTTY_SPEC_CHECKPOINT_PURPOSE_RELEASE && record.confirmed) position.released = true;
        }
        const std::string cp = checkpointName(latest->seq);
        if (!latest->confirmed) {
            const auto verdict = s_.checker.confirmCheck(latest->step, latest->seq);
            position.nextStep = latest->step;
            position.nextAction = cp + " 사용자 검토·확정 대기 (`ks checkpoint confirm " + cp + " --actor user`)." +
                                  (verdict.ok ? "" : " 단, 확정 조건 미충족: `ks checkpoint confirm-check " +
                                                         std::to_string(latest->step) + "`.") +
                                  " 피드백이 있으면 같은 Step 에서 한 라운드 더 진행";
        } else if (earliestOpen != open.end()) {
            position.nextStep = earliestOpen->entryStep;
            position.nextAction = "열린 변경 요청 " + earliestOpen->id + " 반영: " + stepName(earliestOpen->entryStep) +
                                  " 부터 다시 진행 (`ks context cr " + earliestOpen->id + "`)";
        } else if (latest->purpose == KIOTTY_SPEC_CHECKPOINT_PURPOSE_RELEASE) {
            position.nextStep = latest->step;
            position.nextAction = "RELEASE 완료. 이슈가 생기면 변경 요청으로 등록: `ks cr create --source issue ...`";
        } else if (latest->step < 10) {
            position.nextStep = latest->step + 1;
            position.nextAction = stepName(latest->step + 1) + " 시작: `ks context step " + std::to_string(latest->step + 1) + "`";
        } else {
            const auto verdict = s_.checker.releaseCheck();
            position.nextStep = 10;
            position.nextAction = verdict.ok ? "Step 10 확정됨. RELEASE 가능: `ks checkpoint release --actor user`"
                                             : "Step 10 확정됨. RELEASE 조건 미충족: `ks checkpoint release-check`";
        }
    }
    if (position.pendingWrites > 0) {
        position.nextAction += " / 체크포인트에 묶이지 않은 쓰기 " + std::to_string(position.pendingWrites) +
                               "건 있음: 라운드를 마치면 `ks checkpoint create --step " +
                               std::to_string(std::max(position.nextStep, 1)) + "`";
    }
    return position;
}

std::vector<StepRunRecord> Journey::runs() {
    const auto checkpoints = expect(s_.checkpoints.list(0), s_.failure);
    const auto changes = expect(s_.changes.list(static_cast<KiottySpecChangeState>(0)), s_.failure);
    std::vector<StepRunRecord> runs;
    for (const auto &cp : checkpoints) {
        const bool release = cp.purpose == KIOTTY_SPEC_CHECKPOINT_PURPOSE_RELEASE;
        const bool sameRun = !runs.empty() && runs.back().step == cp.step && !release && runs.back().reason != "RELEASE";
        if (!sameRun) {
            StepRunRecord run;
            run.step = cp.step;
            run.firstSeq = cp.seq;
            if (release) {
                run.reason = "RELEASE";
            } else if (runs.empty()) {
                run.reason = "시작";
            } else if (cp.step > runs.back().step) {
                run.reason = cp.step == runs.back().step + 1 ? "다음 단계" : "Step " + std::to_string(runs.back().step) + " 에서 건너뜀";
            } else {
                const int64_t previous = runs.back().lastSeq;
                std::string because;
                for (const auto &change : changes) {
                    if (change.cpOpened > previous && change.cpOpened <= cp.seq) {
                        because += (because.empty() ? "" : ", ") + change.id;
                    }
                }
                run.reason = because.empty() ? "되돌아옴" : because + " 로 되돌아옴";
            }
            runs.push_back(run);
        }
        auto &run = runs.back();
        run.lastSeq = cp.seq;
        ++run.count;
        run.lastNote = cp.note;
        if (cp.confirmed) run.confirmedSeq = cp.seq;
    }
    return runs;
}

std::string Journey::whereAmI(size_t maxBytes) {
    const auto position = this->position();
    std::string out = "# 현재 위치\n\n";
    if (const auto name = expect(meta_.get("project.name"), s_.failure)) out += "- 프로젝트: " + *name + "\n";
    if (position.checkpointSeq) {
        out += "- 체크포인트: " + checkpointName(*position.checkpointSeq) + " (" + stepName(position.step) + ", " +
               (position.confirmed ? "확정" : "미확정") + ")\n";
    } else {
        out += "- 체크포인트: 없음\n";
    }
    if (position.released) out += "- RELEASE 이후 (유지보수)\n";
    out += "- **다음 할 일**: " + position.nextAction + "\n\n";

    out += "## 지나온 단계\n\n";
    const auto history = runs();
    if (history.empty()) {
        out += "없음\n\n";
    } else {
        out += "| # | 단계 | 체크포인트 | 라운드 | 확정 | 이 단계로 온 이유 | 마지막 요약 |\n| --- | --- | --- | --- | --- | --- | --- |\n";
        size_t index = 0;
        for (const auto &run : history) {
            const std::string range = run.firstSeq == run.lastSeq
                                          ? checkpointName(run.firstSeq)
                                          : checkpointName(run.firstSeq) + " ~ " + checkpointName(run.lastSeq);
            out += "| " + std::to_string(++index) + " | " + stepName(run.step) + " | " + range + " | " +
                   std::to_string(run.count) + " | " + (run.confirmedSeq ? checkpointName(*run.confirmedSeq) : "-") +
                   " | " + run.reason + " | " + run.lastNote + " |\n";
        }
        out += "\n";
    }

    if (position.checkpointSeq) {
        const auto checkpoints = expect(s_.checkpoints.list(0), s_.failure);
        const int64_t latest = *position.checkpointSeq;
        int64_t previous = 0;
        for (const auto &cp : checkpoints) {
            if (cp.seq < latest) previous = cp.seq;
        }
        const auto describe = [](const std::vector<ItemChange> &changes) {
            std::string text;
            for (const auto &change : changes) {
                text += "- " + std::string(change.record.removed ? "삭제" : change.added ? "추가" : "수정") + " " +
                        change.record.id + " " + change.record.title + "\n";
            }
            return text.empty() ? std::string("없음\n") : text;
        };
        out += "## 마지막 라운드(" + checkpointName(latest) + ")에서 바뀐 항목\n" +
               describe(expect(s_.items.changedBetween(previous, latest), s_.failure)) + "\n";
        if (position.pendingWrites > 0) {
            out += "## 아직 체크포인트에 묶이지 않은 변경\n" + describe(expect(s_.items.changedBetween(latest, kCurrent), s_.failure)) + "\n";
        }
    }

    std::string open;
    for (const auto &change : expect(s_.changes.list(static_cast<KiottySpecChangeState>(0)), s_.failure)) {
        if (change.state == KIOTTY_SPEC_CHANGE_STATE_OPEN || change.state == KIOTTY_SPEC_CHANGE_STATE_IN_PROGRESS) {
            open += "- " + change.id + " (" + std::string(name(change.state)) + ", 진입 Step " +
                    std::to_string(change.entryStep) + ") " + change.body + "\n";
        }
    }
    if (!open.empty()) out += "## 열린 변경 요청\n" + open + "\n";

    if (position.step >= 1) {
        out += "## " + stepName(position.step) + " 확정 조건\n" + s_.checker.confirmCheck(position.step).report + "\n";
    }
    out += "> 단계 입력은 `ks context step " + std::to_string(std::max(position.nextStep, 1)) + "`, 할 일 목록은 `ks context pending`.\n";

    if (maxBytes > 0 && out.size() > maxBytes) {
        out.resize(maxBytes);
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0U) == 0x80U) out.pop_back();
        if (!out.empty() && (static_cast<unsigned char>(out.back()) & 0x80U) != 0) out.pop_back();
        out += "\n> (한도 초과로 잘림)\n";
    }
    return out;
}

}
