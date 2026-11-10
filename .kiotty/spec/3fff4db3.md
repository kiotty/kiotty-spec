## 시그니처
```c
KiottySpecStatus KiottySpec_Check_orphanTestsBegin(KiottySpec *db, KiottySpecCursor **outCursor);  /* 검증 대상이 사라진 테스트 (getItem) */
```

## 사전조건
- 없음

## 사후조건
- 검증 대상이 사라진 테스트 커서 (getItem)

## 에러 코드
- NULL 핸들·NULL 필수 인자: InvalidArg (상세는 KiottySpec_lastError)

## 소유권
- *outCursor 커서는 호출자가 KiottySpec_Cursor_end
