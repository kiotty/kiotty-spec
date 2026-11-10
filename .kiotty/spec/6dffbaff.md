## 시그니처
```c
KiottySpecStatus KiottySpec_Checkpoint_journeyBegin(KiottySpec *db, KiottySpecCursor **outCursor);  /* 지금까지 밟아온 단계를 연속 구간으로 묶은 이력 (getStepRun) */
```

## 사전조건
- 없음

## 사후조건
- 연속으로 머문 단계 구간 커서 (getStepRun). reason 은 "시작" / "다음 단계" / "CR-### 로 되돌아옴" / "RELEASE"
- 정리로 지운 체크포인트는 나오지 않는다

## 에러 코드
- NULL 핸들·NULL 필수 인자: InvalidArg (상세는 KiottySpec_lastError)

## 소유권
- *outCursor 커서는 호출자가 KiottySpec_Cursor_end
