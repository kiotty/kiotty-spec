# Kiotty-Spec 작업 안내

사양 DB 는 `.kiotty/spec.kisp`, CLI 는 `ks` (`build/Debug/ks.exe`). 진행 상황은 이 문서가 아니라 ks 로 확인한다.

## 지금 어디까지 왔나 (2026-11-12)

- 마지막 체크포인트는 CP-0025 (Step 10, CR-012 반영 후 6.6 다시 판정, 확정). 확정은 사용자가 하거나 사용자가 시킬 때만 한다 (`ks checkpoint complete --actor user`).
- CP-0018 검사 뒤의 계획(CR-001 ~ 006 코드 반영 → CP-0019 ~ 0021 에서 인터페이스 · 설계 · 유닛 테스트 항목 갱신 → CP-0022 에서 다시 검사)을 마쳤다.
- CP-0022 검사에서 남은 FAIL: 2.1 (22건, 다른 C 함수의 enum · relation · checkpoint 인자가 ValueParser 를 지나지 않음), 3.3 (5건, Finder 내보내기 · diff 질의 순회), 4.2 (11건), 4.17 (122건, 계획에서 제외), 6.1 (5건), 6.3 (1건), 6.4 (2건, .kisp 형식 때문). 6.6 은 CR-012 (커서는 C interface 에만) 로 PASS. 다음에 할 일은 사용자와 정한다 (변경 요청 등록 여부 포함).

## 확인할 ks 명령

```sh
ks checkpoint show                                   # 지금 체크포인트
ks checkpoint list                                   # 체크포인트 이력
ks item list --kind change_request --tag 상태        # 변경 요청과 상태
ks item body CR-004                                  # 변경 요청 내용
ks item list --kind code_inspection_result | grep FAIL   # 마지막 코드 검사의 위반 수칙
ks item body CIR-015                                 # 수칙별 위반 위치
```

## 검사 · 빌드

- 정적 검사: `node tools/lint.js` (수칙별 건수), `node tools/lint.js --rule 1.10 --list` (위치).
- 검사 결과 기록 (Step 10 체크포인트 안에서): `node tools/record_inspection.js <수동 검토 결과 파일> [--dry]`. lint 수칙은 다시 돌려 채우고, 수동 검토 수칙은 파일의 `=== <수칙>` 블록(결과 · 근거 · 판단 · 위반 위치)을, 절 항목은 하위 수칙을 합쳐 채운다. 수동 검토 결과 형식은 지금 CIR 본문을 보면 된다.
- 빌드: `cmake --build build --config Debug` (경고 0). 테스트: `build/Debug/kiotty_*_test.exe` 7 개 (system 테스트는 다른 테스트와 동시에 돌리지 않는다).
- 개발 수칙: 사양 DB 의 수칙 항목만 원본이다 (문서 없음, CR-012). `ks item list --kind rule` (번호 · 제목 · 전체 문장), `ks item body RULE-065` (본문), `ks decision show rule` (수칙 결정 기록). 고칠 때는 변경 요청 → Step 2 체크포인트로 한다.
- 편차: `docs/deviations.md`.
