#include "kiotty_guide_text.h"

namespace kiotty::cli {

namespace {

constexpr std::string_view GUIDE_OVERVIEW =
    "ks — 로컬 소프트웨어 라이프사이클 관리 (kiotty-spec)\n"
    "\n"
    "[목적]\n"
    "1. 라이프사이클을 로컬 git 저장소 안에서 관리한다.\n"
    "   사양은 <루트>/.kiotty/spec.kisp 파일 하나 (줄 단위 텍스트, diff · merge 가 쉬움).\n"
    "   코드와 함께 커밋한다.\n"
    "2. AI 가 추적성을 관리하고 필요한 것만 쿼리로 가져온다.\n"
    "   요구사항 → 설계 → 테스트 → 검사 결과가 관계로 이어져 있어,\n"
    "   ks item list · ks impact · ks diff 로 지금 일에 필요한 항목만 읽는다.\n"
    "3. 체크포인트 단위로 관리한다.\n"
    "   모든 쓰기는 한 단계의 체크포인트 안에서 하고, 사용자가 확정하면 그 시점이 고정된다.\n"
    "   - 단계마다 사람이 검토하고 승인한 뒤 다음으로 간다 (AI 가 혼자 앞서가지 않는다).\n"
    "   - 확정 조건이 빠진 연결(인수 조건 · 테스트 짝 · 검사 결과)을 막는다.\n"
    "   - 무엇이 언제 왜 바뀌었는지 체크포인트 사이 diff 로 본다.\n"
    "   - 변경 요청은 필요한 단계부터만 다시 밟는다.\n"
    "\n"
    "[역할]\n"
    "- AI: 체크포인트 시작, 항목 작성, 구현, 테스트 · 검사 실행과 결과 기록.\n"
    "- 사용자만 (--actor user): checkpoint complete · prune, remote push · accept · discard.\n"
    "  AI 는 [user] 가 붙은 명령을 실행하지 않고 사용자에게 요청한다.\n"
    "\n"
    "[12 단계]\n"
    " 1 변경 요청 접수     무엇을 왜 바꿀지 받는다                 CR\n"
    " 2 요구사항           해야 할 일과 지킬 개발 수칙을 정한다    REQ, RULE, PRULE\n"
    " 3 외부 인터페이스    밖에서 보이는 약속을 정한다             UI\n"
    " 4 설계               어떻게 만들지 정한다                    DSN\n"
    " 5 유닛 테스트 계획   설계 단위 검증 방법을 정한다            UT\n"
    " 6 개발 계획          구현 순서를 마일스톤으로 묶는다         MS\n"
    " 7 구현               코드와 유닛 테스트를 쓰고 돌린다        UTR\n"
    " 8 시스템 테스트 계획 요구사항을 실행으로 검증할 시나리오     ST\n"
    " 9 시스템 테스트 실행 시스템 테스트를 돌리고 결과를 남긴다    STR\n"
    "10 코드 검사          코드가 수칙을 지키는지 수칙마다 판정    CIR\n"
    "11 E2E 테스트 계획    외부 인터페이스를 끝까지 쓰는 시나리오  E2E\n"
    "12 E2E 테스트 실행    E2E 를 돌리고 변경 요청을 닫는다        E2ER\n"
    "단계별 항목 · 본문 · 확정 조건: ks guide step N    Kind 별: ks guide kind KIND\n"
    "\n"
    "[작업 루프]\n"
    "1. ks next 로 위치와 할 일을 본다.\n"
    "2. 앞 단계가 확정됐으면 ks checkpoint start N --description \"...\"\n"
    "3. ks item add/set · ks tag set · ks relation add 로 이 단계의 항목을 쓴다.\n"
    "4. ks next 에 작업할 항목이 없으면 사용자에게 확정을 요청한다.\n"
    "\n"
    "[규칙]\n"
    "- 출력은 TSV, 첫 줄이 머리글. 쓰기 명령은 성공하면 만든 ID 만 찍는다.\n"
    "- 본문은 Markdown. spec.kisp 를 직접 고치지 않는다.\n"
    "- 태그 · 관계 이름: ks model KIND, 작성 규칙: ks decision show KIND.\n"
    "- 개발 수칙 공유: ks guide remote\n";

constexpr std::string_view GUIDE_REMOTE =
    "[개발 수칙 원격 공유]\n"
    "개발 수칙(rule)만 git 원격 저장소로 여러 프로젝트가 나눈다.\n"
    "전용 개발 수칙(private_rule)은 나가지 않고, 공통 수칙보다 우선한다.\n"
    "원격 복제본은 <루트>/.kiotty/rule/ (git 제외).\n"
    "\n"
    "ks remote open URL                 원격 주소를 기억하고 복제본을 만든다\n"
    "ks remote check rule               원격이 기준 버전과 같은지 본다 (same | changed)\n"
    "ks remote pull rule                받아 두고 추가 · 삭제 · 변경 수를 찍는다\n"
    "ks remote list rule                받아 둔 수칙 목록\n"
    "ks remote diff rule [ID...] [--body]  지금 수칙과의 차이\n"
    "ks remote body rule ID             받아 둔 수칙 하나의 본문\n"
    "[user] ks remote accept rule --actor user   새 체크포인트와 변경 요청으로 편입한다\n"
    "[user] ks remote discard rule --actor user  받아 둔 수칙을 버린다\n"
    "[user] ks remote push rule --actor user     확정된 개발 수칙을 올린다\n"
    "ks remote close                    원격을 잊고 복제본을 지운다\n"
    "\n"
    "받기: check → pull → diff --body 로 차이를 요약 → 사용자가 accept 또는 discard.\n"
    "편입하면 Step 1 체크포인트와 변경 요청이 생기고, 코드 검사부터 다시 돈다.\n";

constexpr std::string_view SHORT_STEP_NAMES[] = {
    "변경 요청", "요구사항",       "외부 인터페이스", "설계",      "유닛 테스트", "개발 계획",
    "구현",      "시스템 테스트 계획", "시스템 테스트", "코드 검사", "E2E 계획",   "E2E 실행"
};

constexpr StepGuide STEP_GUIDES[] = {
    { "무엇을 왜 바꿀지 받는다", "사용자 요청, 열린 변경 요청 (ks item list --kind change_request)",
      "영향 받는 항목을 ks relation add CR-xxx \"영향 요구사항\" REQ-xxx 처럼 잇는다",
      "열린 변경 요청마다 진입 단계 (어느 단계부터 다시 밟을지)" },
    { "해야 할 일과 지킬 개발 수칙을 정한다", "열린 변경 요청 본문 (ks item body CR-xxx)",
      "ks relation add CR-xxx \"영향 요구사항\" REQ-xxx (\"영향 개발 수칙\" RULE-xxx)",
      "기능 요구사항마다 ## 인수 조건 목록 1 개 이상" },
    { "밖에서 보이는 약속을 정한다", "이번 요구사항 (ks item list --kind requirement)",
      "ks relation add CR-xxx \"영향 외부 인터페이스\" UI-xxx", "없음" },
    { "어떻게 만들지 정한다. 본문을 고치면 구현 상태가 변경 필요가 된다",
      "요구사항과 외부 인터페이스", "ks relation add CR-xxx \"영향 설계 사항\" DSN-xxx", "없음" },
    { "설계 단위 검증 방법을 정한다", "이번 설계 (ks item list --kind design --tag 구현 상태)",
      "ks relation add UT-xxx \"검증 설계 사항\" DSN-xxx", "없음" },
    { "구현 순서를 마일스톤으로 묶는다", "구현 상태가 미구현 · 변경 필요인 설계와 유닛 테스트",
      "ks relation add CR-xxx \"관련 마일스톤\" MS-xxx",
      "모든 설계 사항과 유닛 테스트가 어떤 마일스톤에 포함" },
    { "코드와 유닛 테스트를 쓰고 돌린다", "마일스톤 본문의 순서와 끝났다는 기준",
      "끝난 설계 · 유닛 테스트는 ks tag set ID \"구현 상태\" \"구현 완료\"", "없음" },
    { "요구사항을 실행으로 검증할 시나리오를 정한다", "기능 요구사항의 인수 조건",
      "ks relation add ST-xxx \"검증 요구사항\" REQ-xxx",
      "기능 요구사항마다 시스템 테스트 하나 (1:1)" },
    { "시스템 테스트를 돌리고 결과를 남긴다", "시스템 테스트 본문의 절차",
      "ks relation add STR-xxx \"대상 시스템 테스트\" ST-xxx", "없음 (실패는 고치고 다시 돌린다)" },
    { "코드가 개발 수칙을 지키는지 수칙마다 판정한다",
      "ks item list --kind rule, ks item list --kind private_rule",
      "ks relation add CIR-xxx \"검사 개발 수칙\" RULE-xxx (\"검사 전용 개발 수칙\" PRULE-xxx)",
      "대체되지 않은 개발 수칙과 모든 전용 개발 수칙이 검사 결과와 1:1" },
    { "외부 인터페이스를 끝까지 쓰는 시나리오를 정한다", "상위 없는 외부 인터페이스 (묶음)",
      "ks relation add E2E-xxx \"검증 외부 인터페이스\" UI-xxx",
      "상위 없는 외부 인터페이스마다 E2E 테스트 하나" },
    { "E2E 테스트를 돌리고 변경 요청을 닫는다", "E2E 테스트 본문의 절차",
      "ks relation add E2ER-xxx \"대상 E2E 테스트\" E2E-xxx, ks tag set CR-xxx 상태 완료",
      "모든 결과 PASS, 결과마다 대상 하나, 열린 변경 요청 없음" },
};

constexpr KindGuide KIND_GUIDES[] = {
    { "무엇을 왜 바꿀지 한 줄", "목적, 결정 (고른 안과 다른 안), 범위, 단계 계획" },
    { "할 일을 검증 가능한 한 문장으로",
      "## 진술 / ## 근거 시나리오 / ## 인수 조건 (- 전제 조건: / 행동: / 기대 결과:)" },
    { "코드가 지킬 공통 개발 수칙. 다른 프로젝트와 공유한다 (ks guide remote)",
      "이유, 지키는 예 · 어기는 예, 예외" },
    { "함수 · 타입 · 명령 · 화면 이름. 상위 없는 항목은 묶음",
      "시그니처, 사전조건, 사후조건, 에러 코드, 소유권" },
    { "모듈 · 디렉토리 · 흐름 이름", "역할, 구성, 의존, 이렇게 한 이유" },
    { "검증할 동작 하나", "## 대상 / ## 전제 조건 / ## 행동 / ## 기대 결과" },
    { "구현 묶음 이름", "## 목표 / ## 순서 / ## 끝났다는 기준" },
    { "유닛 테스트 실행 결과", "실행 명령, 실패 내용" },
    { "기능 요구사항 하나를 실행으로 검증할 시나리오", "준비, 절차, 기대 결과 (인수 조건과 대응)" },
    { "시스템 테스트 실행 결과", "실행 명령, 실패 내용" },
    { "개발 수칙 하나의 검사 결과", "판단 근거, 위반 위치" },
    { "외부 인터페이스 묶음을 끝까지 쓰는 시나리오", "준비, 절차, 기대 결과" },
    { "E2E 테스트 실행 결과", "실행 명령, 실패 내용" },
    { "이 프로젝트에만 유효한 수칙. 공통 수칙보다 우선하고 공유하지 않는다",
      "범위, 공통 수칙과 다른 이유. 이기는 공통 수칙이 없으면 더하는 수칙" },
};

constexpr ConditionText CONDITION_TEXTS[] = {
    { "진입 단계 없음", "어느 단계부터 다시 밟을지 정한다",
        "ks tag set {0} \"진입 단계\" \"Step N\"", "", "" },
    { "인수 조건 없음", "본문에 ## 인수 조건 목록을 쓴다", "ks item set {0} --body-file F", "",
        "" },
    { "마일스톤에 없음", "마일스톤에 넣는다",
        "ks relation add MS-xxx \"포함 설계 사항\"|\"포함 유닛 테스트\" {0}", "", "" },
    { "시스템 테스트 없음", "시스템 테스트를 만든다",
        "ks item add system_test_plan --title \"...\"", "시스템 테스트를 요구사항에 잇는다",
        "ks relation add ST-xxx \"검증 요구사항\" {0}" },
    { "E2E 테스트 없음", "E2E 테스트를 만든다", "ks item add e2e_test_plan --title \"...\"",
        "E2E 테스트를 외부 인터페이스에 잇는다",
        "ks relation add E2E-xxx \"검증 외부 인터페이스\" {0}" },
    { "검사 결과 없음", "수칙을 검사하고 결과를 만든다",
        "ks item add code_inspection_result --title \"...\" --tag 결과=PASS",
        "결과를 수칙에 잇는다", "ks relation add CIR-xxx \"{1}\" {0}" },
    { "대상이 하나가 아님", "대상 관계를 보고 하나만 남긴다", "ks relation list {0}", "", "" },
    { "PASS 아님", "고치고 다시 실행한 뒤 결과를 바꾼다", "ks tag set {0} 결과 PASS", "",
        "" },
    { "열린 변경 요청", "변경 요청을 닫는다", "ks tag set {0} 상태 완료", "", "" },
};

}

std::string_view findGuideOverview() { return GUIDE_OVERVIEW; }

std::string_view findGuideRemote() { return GUIDE_REMOTE; }

std::string_view findShortStepName(uint32_t step)
{
    uint32_t position = 1;
    for (const std::string_view name : SHORT_STEP_NAMES) {
        if (position == step) { return name; }
        ++position;
    }
    return std::string_view();
}

std::optional<StepGuide> findStepGuide(uint32_t step)
{
    uint32_t position = 1;
    for (const StepGuide& guide : STEP_GUIDES) {
        if (position == step) { return guide; }
        ++position;
    }
    return std::nullopt;
}

std::optional<KindGuide> findKindGuide(uint32_t kind)
{
    uint32_t position = 1;
    for (const KindGuide& guide : KIND_GUIDES) {
        if (position == kind) { return guide; }
        ++position;
    }
    return std::nullopt;
}

std::optional<ConditionText> findConditionText(uint32_t condition)
{
    uint32_t position = 1;
    for (const ConditionText& text : CONDITION_TEXTS) {
        if (position == condition) { return text; }
        ++position;
    }
    return std::nullopt;
}

}
