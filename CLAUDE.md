# Kiotty-Spec 작업 안내

사양 DB 는 `.kiotty/spec.kisp`, CLI 는 `ks` (`build/Debug/ks.exe`). 진행 상황은 이 문서가 아니라 ks 로 확인한다.

## 지금 어디까지 왔나 (2026-11-12)

- 마지막 체크포인트는 CP-0021 (Step 5 유닛 테스트 계획, 확정). 확정은 사용자가 하거나 사용자가 시킬 때만 한다 (`ks checkpoint complete --actor user`).
- CP-0018 검사 뒤 남은 위반을 고치는 중이다. 계획:
  1. 변경 요청 CR-001 ~ CR-006 을 코드부터 하나씩 해결한다 (4.17 메서드 이름 수칙은 제외).
  2. 다음 체크포인트에서 바뀐 모듈의 요구사항 · 설계 · 유닛 테스트 항목을 갱신한다.
  3. 그다음 체크포인트에서 코드 검사를 다시 돌린다.
- 완료: CR-001 ~ CR-011 모두 코드에 반영했다 (계획 1 끝). 남은 lint 위반은 4.17 이 대부분이다.
- 계획 2 끝: CP-0019 (외부 인터페이스 UI-*), CP-0020 (설계 DSN-*), CP-0021 (유닛 테스트 계획 UT-*) 에서 항목을 코드에 맞췄다. 요구사항은 바뀌지 않았다 (CR 진입 단계가 3 · 4). 갱신 스크립트는 git 밖의 `temp/ui_*.js`, `temp/dsn_v3/`, `temp/ut_update.js`.
- 다음: 계획 3. Step 10 체크포인트를 열고 코드 검사를 다시 돌린다. `build/record_inspection.py` 는 Python 이 필요한데 이 PC 에는 없다 (Store 바로가기만 있음).

## 확인할 ks 명령

```sh
ks checkpoint show                                   # 지금 체크포인트
ks checkpoint list                                   # 체크포인트 이력
ks item list --kind change_request --tag 상태        # 변경 요청과 상태
ks item body CR-004                                  # 변경 요청 내용
ks item list --kind code_inspection_result | grep FAIL   # 마지막 코드 검사의 위반 수칙
ks item body CIR-013                                 # 수칙별 위반 위치
```

## 검사 · 빌드

- 정적 검사: `node tools/lint.js` (수칙별 건수), `node tools/lint.js --rule 1.10 --list` (위치).
- 검사 결과 기록: `python build/record_inspection.py <lint --list 출력> build/manual_inspection.json --update`.
- 빌드: `cmake --build build --config Debug` (경고 0). 테스트: `build/Debug/kiotty_*_test.exe` 7 개 (system 테스트는 다른 테스트와 동시에 돌리지 않는다).
- 개발 수칙: `docs/requirements/개발수칙.md`, 편차: `docs/deviations.md`.
