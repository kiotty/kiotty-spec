## 시그니처
```c
KiottySpecStatus KiottySpec_Transaction_rollback(KiottySpec *db);  /* 트랜잭션 롤백 */
```

## 사전조건
- Transaction_start 뒤

## 사후조건
- 묶은 쓰기를 모두 버린다

## 에러 코드
- 열린 트랜잭션 없음: Db
- NULL 핸들·NULL 필수 인자: InvalidArg (상세는 KiottySpec_lastError)

## 소유권
- 돌려받아 해제할 메모리 없음
