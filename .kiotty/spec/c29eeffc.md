## 시그니처
```c
KiottySpecStatus KiottySpec_Item_listBegin(KiottySpec *db, const KiottySpecItemFilter *filter, KiottySpecCursor **outCursor);  /* 필터로 항목 열거 (getItem) */
```

## 사전조건
- filter 는 NULL(전체) 또는 0·NULL 필드는 무시

## 사후조건
- kind·category·parentId·text·atCheckpoint 로 거른 항목 커서 (getItem)

## 에러 코드
- 없는 atCheckpoint: NotFound
- NULL 핸들·NULL 필수 인자: InvalidArg (상세는 KiottySpec_lastError)

## 소유권
- *outCursor 커서는 호출자가 KiottySpec_Cursor_end
