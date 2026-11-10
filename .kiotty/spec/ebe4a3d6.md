## 개요
함수 6 개를 묶은 개념 단위. 함수별 계약은 하위 항목에 있고, 여기에는 묶음 공통의 타입과 규약을 둔다.

## 하위 함수
- UI-110 KiottySpec_Check_requirementsWithoutCriteriaBegin
- UI-111 KiottySpec_Check_systemTestOneToOne
- UI-112 KiottySpec_Check_e2eTestOneToOne
- UI-113 KiottySpec_Check_unlinkedBegin
- UI-114 KiottySpec_Check_orphanTestsBegin
- UI-115 KiottySpec_Check_all

## 시그니처
```c
(이 묶음 고유의 타입 없음, 공통 타입은 UI-001)
```

## 사전조건
- 없음

## 사후조건
- 기록을 바꾸지 않는다
- 인수 조건 없는 기능 요구사항(R-4), 시스템·E2E 1:1 의 없음/둘 이상(R-3), 연결 없는 항목, 대상 없는 테스트
- all: 위 검사 + 마일스톤 포함(R-5) + 결정 기록 검사를 종류별 개수·목록 Markdown 으로

## 에러 코드
- NULL 핸들·NULL 필수 인자는 InvalidArg, 상세는 KiottySpec_lastError

## 소유권
- all 의 보고서는 호출자가 KiottySpec_free
- 커서(1:1 검사는 둘)는 각각 Cursor_end
