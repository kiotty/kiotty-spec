## 시그니처
```c
KiottySpecStatus KiottySpec_Item_exists(KiottySpec *db, const char *itemId, bool *outExists);  /* 현재 뷰에 존재하는지 */
```

## 사전조건
- 없음

## 사후조건
- *outExists 에 현재 뷰 존재 여부. 없어도 Ok

## 에러 코드
- NULL 핸들·NULL 필수 인자: InvalidArg (상세는 KiottySpec_lastError)

## 소유권
- 돌려받아 해제할 메모리 없음 (out 값은 호출자 변수에 복사)
- 입력 문자열은 호출 동안만 읽는다
