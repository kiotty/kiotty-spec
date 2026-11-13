# Kiotty-Spec 작업 안내

사양 DB 는 `.kiotty/spec.kisp`, CLI 는 `ks` (`build/Debug/ks.exe`). 진행 상황은 이 문서가 아니라 ks 로 확인한다.

## 지금 어디까지 왔나 (2026-11-13, v0.0.0 이후)

- v0.0.0 (태그) 에서 12 단계를 한 바퀴 다 돌았다. 그 뒤 CP-0041 이전 기록을 prune 했다 (이전 이력은 v0.0.0 태그의 spec.kisp). 확정은 사용자가 하거나 사용자가 시킬 때만 한다 (`ks checkpoint complete --actor user`).
- CI: `.github/workflows/build.yml` 이 Linux · macOS · Windows 에서 빌드하고 테스트 8 개를 돌린다.
- 진행 중: CP-0042 (Step 1) 의 CR-015 공통 개발 수칙 원격 공유. 계획은 docs/plan/261111_rule_sync_v1.md, 결정 D1 ~ D3 · D5 · D6 은 아직 확인받지 않았다.
- 시스템 테스트 31 개, E2E 19 개(tests/e2e/e2e_test.c, 외부 인터페이스 묶음과 1:1, CR-013) 모두 PASS. 코드 검사 결과도 모두 PASS.
- CR-014 (2026-11-13): 2.1 은 고쳤고(QueryParser · RangeParser), 3.3 · 4.2 · 4.17 · 6.1 · 6.3 · 6.4 는 docs/deviations.md 의 편차다. 4.17 은 tools/lint_accepted.txt 의 기존 이름만 받아들이고 새 이름은 검사한다.

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
