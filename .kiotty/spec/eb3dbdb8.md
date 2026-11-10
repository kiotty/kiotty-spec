## 시그니처
```c
KiottySpecStatus KiottySpec_File_get(KiottySpec *db, const char *fileId, KiottySpecFile **out);  /* 파일 하나 조회 */
```

## 사전조건
- 없음

## 사후조건
- *out 에 파일

## 에러 코드
- 없는 FILE: NotFound
- NULL 핸들·NULL 필수 인자: InvalidArg (상세는 KiottySpec_lastError)

## 소유권
- *out 는 호출자가 KiottySpec_free
- 입력 문자열은 호출 동안만 읽는다
