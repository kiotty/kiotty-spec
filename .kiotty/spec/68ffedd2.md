## 시그니처
```c
KiottySpecStatus KiottySpec_Milestone_progress(KiottySpec *db, size_t *outTotal, size_t *outImplemented, size_t *outNeedsChange);  /* 전체 포함 항목 수와 상태별 집계 */
```

## 사전조건
- 없음

## 사후조건
- *outTotal·*outImplemented·*outNeedsChange 에 전체 포함 항목 수와 상태별 수

## 에러 코드
- NULL 핸들·NULL 필수 인자: InvalidArg (상세는 KiottySpec_lastError)

## 소유권
- 돌려받아 해제할 메모리 없음 (out 값은 호출자 변수에 복사)
