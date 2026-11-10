## 시그니처
```c
KiottySpecStatus KiottySpec_Context_status(KiottySpec *db, char **outMarkdown);  /* 현재 단계, 마지막 확정 체크포인트, 마일스톤 진행률, 테스트 집계를 한 화면으로 */
```

## 사전조건
- 없음

## 사후조건
- 항목 수, 마지막 확정, 마일스톤 진행률, 테스트·검사 집계, 열린 변경 요청 수를 Markdown 으로

## 에러 코드
- NULL 핸들·NULL 필수 인자: InvalidArg (상세는 KiottySpec_lastError)

## 소유권
- *outMarkdown 는 호출자가 KiottySpec_free
