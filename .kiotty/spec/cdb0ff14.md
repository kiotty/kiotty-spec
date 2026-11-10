## 개요
함수 4 개를 묶은 개념 단위. 함수별 계약은 하위 항목에 있고, 여기에는 묶음 공통의 타입과 규약을 둔다.

## 하위 함수
- UI-100 KiottySpec_Decision_get
- UI-101 KiottySpec_Decision_getAt
- UI-102 KiottySpec_Decision_rewrite
- UI-103 KiottySpec_Decision_check

## 시그니처
```c
typedef enum KiottySpecDecisionKind {
    KiottySpecDecisionKind_Requirement = 1,  /* 결정 기록: 요구사항 */
    KiottySpecDecisionKind_Rule,             /* 결정 기록: 개발 규칙 */
    KiottySpecDecisionKind_Interface,        /* 결정 기록: 외부 인터페이스 */
    KiottySpecDecisionKind_Design,           /* 결정 기록: 설계 */
    KiottySpecDecisionKind_UnitTest,         /* 결정 기록: 유닛 테스트 */
    KiottySpecDecisionKind_Plan              /* 결정 기록: 개발 계획 */
} KiottySpecDecisionKind;
```

## 사전조건
- rewrite 의 markdown 은 줄마다 "- ID · 관심사 : 결정 (이유) [기각: …]"

## 사후조건
- rewrite: 종류의 결정 기록을 통째로 바꿔 현재 라운드에 쓴다. 이전 체크포인트 시점은 그대로
- check: 사라진 항목을 가리키는 줄과 한도 초과 줄 보고

## 에러 코드
- 한 줄이 decision_max_line(기본 200 자) 초과, 항목 하나의 관심사가 decision_max_concerns(기본 8) 초과: InvalidArg (R-7)
- 없는 CP: NotFound
- NULL 핸들·NULL 필수 인자는 InvalidArg, 상세는 KiottySpec_lastError

## 소유권
- get·getAt·check 의 Markdown 은 호출자가 KiottySpec_free
