## 시그니처
```c
KiottySpecStatus KiottySpec_Export_jsonl(KiottySpec *db, const char *dir);  /* DB 를 테이블별 jsonl(ID 정렬)로 내보낸다. pre-commit 훅 */
```

## 사전조건
- dir 은 NULL·빈 문자열(.kiotty/spec) 또는 프로젝트 안 디렉토리

## 사후조건
- 테이블별 jsonl 을 ID 순으로 쓴다. 같은 기록이면 같은 바이트 (REQ-024). pre-commit 훅이 부른다

## 에러 코드
- 디렉토리·파일 쓰기 실패: Io
- NULL 핸들·NULL 필수 인자: InvalidArg (상세는 KiottySpec_lastError)

## 소유권
- 돌려받아 해제할 메모리 없음
- 입력 문자열은 호출 동안만 읽는다
