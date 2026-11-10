## 시그니처
```c
KiottySpecStatus KiottySpec_Result_last(KiottySpec *db, const char *targetId, KiottySpecResult **out);  /* 대상의 마지막 결과 */
```

## 사전조건
- 없음

## 사후조건
- *out 에 대상의 가장 나중 결과

## 에러 코드
- 결과 없음: NotFound
- NULL 핸들·NULL 필수 인자: InvalidArg (상세는 KiottySpec_lastError)

## 소유권
- *out 는 호출자가 KiottySpec_free
- 입력 문자열은 호출 동안만 읽는다
