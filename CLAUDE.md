# Kiotty-Spec 작업 안내

사양 DB 는 `.kiotty/spec.kisp`, CLI 는 `ks` (`build/Debug/ks.exe`). 진행 상황은 이 문서가 아니라 ks 로 확인한다.

## 지금 어디까지 왔나 (2026-11-13, v0.0.0 이후)

- v0.0.0 (태그) 에서 12 단계를 한 바퀴 다 돌았다. 그 뒤 CP-0041 이전 기록을 prune 했다 (이전 이력은 v0.0.0 태그의 spec.kisp). 확정은 사용자가 하거나 사용자가 시킬 때만 한다 (`ks checkpoint complete --actor user`).
- CI: `.github/workflows/build.yml` 이 Linux · macOS · Windows 에서 빌드하고 테스트 9 개(rule_sync 포함)를 돌린다.
- 진행 중: CR-015 공통 개발 수칙 원격 공유. 계획 전체는 `ks item body CR-015` (별도 문서 없음). Step 1 ~ 6 (CP-0042 ~ CP-0047) 확정, 지금 CP-0048 (Step 7 구현) 이 RUNNING.
  - 끝남 (마일스톤 MS-003 ~ MS-005): R0 모델 2 (전용 개발 수칙 `private_rule` · PRULE, 관계 3 개, 백로그 · 공통 수칙 값, model 1 파일 열기), R1 `readRemote` · `writeRemote` · 수칙 비교 커서 · 본문 줄 단위 비교 커서 · `exportRules`, R2 `acceptRules` (쓰기 한 번, 실패 시 파일 다시 읽어 되돌림). 테스트는 `tests/rule_sync/rule_sync_test.cpp`. 빌드 경고 0, 테스트 9 개 PASS, lint 0.
  - 다음 (MS-006, R3 · R4): 명령줄 `ks remote` 10 개 (open · close · push · check · pull · list · diff [--body] · body · accept · discard). CR-015 본문 1 장 · 1.1 ~ 1.4 · 3.2 를 따른다. `app/cli/src/infra/` 에 git 실행기 (Windows `CreateProcessW`, POSIX `posix_spawnp`, 시간 제한, 종료 코드 · stdout), `presentation/handler/kiotty_remote_handler.cpp` 에 명령, `kiotty_main_handler.cpp` 의 표에 등록, 메시지는 `i18n/kiotty_messages`. 받아 둔 파일은 `.kiotty/remote/rule.kisp` 를 두 번째 Context 로 연다 (`KiottySpec_open` 은 없으면 만드므로 먼저 존재 확인). Kind 인자는 `rule` 만 (UT-092 `testParseRemoteKind`, tests/cli/cli_test.cpp). accept 는 CLI 가 변경 요청 본문 (원격 URL · commit, 수칙 diff, 전용 개발 수칙 처리 표 `처리 = 미정`) 을 만들어 넘기고, 영향받은 PRULE 은 accept 전에 "대체 개발 수칙" 관계로, 지울 검사 결과는 "검사 개발 수칙" 관계로 구해 찍는다. 진입 단계 = min(10, 다음 단계).
  - 그 뒤: 사양 DB 정리 — UI-160 에러에 POOL_IS_FULL, UI-163 필드 순서 title 먼저, 공개 헤더 `kiotty_spec_sync_records.h`, RuleExporter 는 context 층으로 이동, 새 모듈 (spec_line · check_values · rule_compare · `ItemReader::bodyOf` · `kiotty_checker_pairs.cpp`) 을 설계에 반영, 구현한 설계 · 유닛 테스트를 `구현 완료` 로, `cmake --install` 로 ks 재설치. 그 다음 사용자에게 CP-0048 확정 요청. 이후 Step 8 시스템 테스트 계획 (임시 `git init --bare` 시나리오), Step 10 과 R5 (docs/deviations.md 편차를 PRULE 로), R6 첫 실제 push.
  - 미정: MS-002 (옛 미실행 테스트 UT-014 · 033 · 050 ~ 054) 처리는 사용자가 아직 정하지 않았다.
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
- 빌드: `cmake --build build --config Debug` (경고 0). 설치: `cmake --install build --config Debug` 가 ks 를 install.local (git 제외) 의 `INSTALL_PATH=<디렉토리>` 로 복사한다. 테스트: `build/Debug/kiotty_*_test.exe` 7 개 (system 테스트는 다른 테스트와 동시에 돌리지 않는다).
- 개발 수칙: 사양 DB 의 수칙 항목만 원본이다 (문서 없음, CR-012). `ks item list --kind rule` (번호 · 제목 · 전체 문장), `ks item body RULE-065` (본문), `ks decision show rule` (수칙 결정 기록). 고칠 때는 변경 요청 → Step 2 체크포인트로 한다.
- 편차: `docs/deviations.md`.
