# 공통 개발 수칙 공유 계획 v1 (2026-11-11)

목적: 한 프로젝트에서 바꾼 개발 수칙(`rule` Kind)을 원격 저장소에 올리고, 다른 프로젝트가 원할 때 받아 새 체크포인트로 편입한다. 자동으로 받지 않는다. 수칙은 체크포인트 이력의 일부라서, 받는 시점을 사용자가 정해야 한다.

## 0. 확인받을 결정

| 번호 | 결정 | 권장안 | 다른 안 |
| --- | --- | --- | --- |
| D1 | 원격 | GitHub 공개 저장소, HTTPS URL, `git` 명령을 실행해서 다룬다 (2 장) | 클라우드 동기화 폴더 경로 (OneDrive 등) |
| D2 | 공유 범위 | `rule` Kind 전체. 프로젝트별 예외는 `docs/deviations.md` | 공통 태그가 달린 수칙만 |
| D3 | accept 가 만드는 체크포인트 | 1 단계 (변경 요청 접수) 와 변경 요청 항목 하나. 원격 commit 과 바뀐 수칙을 "영향 개발 수칙" 관계로 남긴다 | 2 단계 (요구사항), 변경 요청 없이 |
| D4 | push 조건 | **확정 (사용자 2026-11-11)**: 가장 최근 체크포인트가 완료(DONE)일 때만. 진행 중인 수칙 수정은 올리지 않는다 | — |
| D5 | push 충돌 | 마지막 accept · push 뒤에 원격이 바뀌었으면 거부. 먼저 pull · accept | 덮어쓴다 |
| D6 | 권한 | `push` · `accept` · `discard` 는 `--actor user` 만 | 제한 없음 |

## 1. 사용 흐름

`ks remote <동사> <Kind>` 형식이다. v1 의 Kind 는 `rule` 하나지만 나중에 다른 Kind 도 같은 명령으로 공유할 수 있게 자리를 둔다.

### 1.1 수칙을 바꾼 프로젝트

| 명령 | 하는 일 | 출력 |
| --- | --- | --- |
| `ks remote open URL` | URL 을 meta `remote.url` 에 기억한다 | 없음 |
| `ks remote close` | URL 과 받아 둔 수칙을 잊는다 | 없음 |
| `ks remote push rule --actor user` | 가장 최근 체크포인트(완료)의 수칙으로 원격 `rule.kisp` 를 다시 쓰고 commit · push 한다. meta `remote.rule.base` 를 새 commit 으로 | 새 commit |

### 1.2 수칙 변경을 모르는 프로젝트

| 명령 | 하는 일 | 출력 |
| --- | --- | --- |
| `ks remote open URL` / `close` | 1.1 과 같다 | 없음 |
| `ks remote check rule` | 원격 commit 이 `remote.rule.base` 와 같은지만 본다 (`git ls-remote`, 내려받지 않음) | `same` 또는 `changed  <commit>` |
| `ks remote pull rule` | 원격 `rule.kisp` 를 `.kiotty/remote/rule.kisp` 에 받아 둔다 | 추가 · 삭제 · 변경 개수 한 줄 |
| `ks remote list rule` | 받아 둔 수칙 목록 | `ks item list` 와 같은 TSV |
| `ks remote diff rule [ID...]` | 로컬 최신 체크포인트의 수칙과 받아 둔 수칙 비교 | TSV: `id  change  fields  title` |
| `ks remote body rule ID` | 받아 둔 수칙의 본문 | 본문 |
| `ks remote accept rule --actor user` | 새 체크포인트를 만들고 받아 둔 수칙으로 로컬 수칙을 바꾼다 | 새 체크포인트, 지운 수칙을 가리키던 항목 ID |
| `ks remote discard rule --actor user` | 받아 둔 수칙을 지운다 | 없음 |

- `change` 는 `added` (원격에만) · `removed` (로컬에만) · `modified`, `fields` 는 `title` `summary` `parent` `tag` `body` 중 다른 것.
- `diff` 에 ID 를 주면 그 수칙만 본다.
- `.kiotty/remote/` 는 커밋하지 않는다. `pull` 이 `.kiotty/.gitignore` 에 `remote/` 를 넣는다.

### 1.3 accept

조건 (하나라도 안 맞으면 거부):

- 가장 최근 체크포인트가 완료(DONE)다.
- 받아 둔 수칙이 있고, 로컬과 다르다.
- 받아 둔 파일의 모델 버전이 이 `ks` 와 같다.

하는 일 (쓰기 한 번, 실패하면 전부 되돌린다):

1. 새 체크포인트를 만든다 (D3: 1 단계, 설명 `공통 개발 수칙 <commit 앞 7자리>`).
2. 변경 요청 항목을 만든다.
   - 본문: 원격 URL · commit 과 diff.
   - 태그: 상태 `열림`, 출처 `공통 수칙` (5 장에서 더하는 값), 진입 단계는 아래 규칙.
   - 관계: 추가 · 변경된 수칙에 "영향 개발 수칙".
   - 진입 단계 = min(10, accept 직전의 다음 단계). 코드 검사까지 간 프로젝트는 10 단계로 돌아가 다시 검사하고, 아직 그 전인 프로젝트는 하던 단계를 이어 하다가 10 단계에서 새 수칙으로 검사한다.
3. `added` 는 원격 ID 그대로 만든다 (부모가 먼저). `modified` 는 필드를 원격 값으로. `removed` 는 지운다.
4. 변경 · 삭제된 수칙의 코드 검사 결과를 지운다. 옛 수칙으로 한 검사라서 남겨 두면 10 단계의 짝 조건(수칙마다 검사 결과 1 개)을 그냥 통과한다. 지우면 그 조건이 다시 검사할 수칙을 정확히 가리킨다. 지운 결과는 과거 체크포인트에 남는다. 지운 ID 를 찍는다.
5. `remote.rule.base` 를 받아 둔 commit 으로 하고 받아 둔 파일을 지운다.

체크포인트는 진행 중(RUNNING)으로 남는다. 사용자가 `ks diff` 로 확인하고 `ks checkpoint complete --actor user` 로 닫는다. 1 단계의 확정 조건(열린 변경 요청마다 진입 단계가 있다)은 accept 가 이미 채웠다.

#### 받은 뒤의 흐름 (코드 검사까지 마친 프로젝트)

| 순서 | 체크포인트 | 하는 일 | 확정 조건 |
| --- | --- | --- | --- |
| 1 | `ks remote accept rule` → 1 단계 (RUNNING) | 수칙 편입, 변경 요청 `열림` · 진입 단계 10, 낡은 검사 결과 삭제 | 진입 단계 있음 (자동) |
| 2 | `ks checkpoint complete` | 사용자 확인 | — |
| 3 | `ks where` | 다음 단계 = 10 (열린 변경 요청의 가장 상류 진입 단계) | — |
| 4 | `ks checkpoint start 10` | 추가 · 변경된 수칙만 다시 검사, 검사 결과 추가 | 모든 수칙에 검사 결과 1 개 |
| 5 | FAIL 이 있으면 | 코드를 고치고 다시 검사 (지금의 FAIL 처리와 같다) | — |
| 6 | 모두 PASS | 변경 요청 상태 `완료`, `ks checkpoint complete` | — |

2 단계(요구사항)로 편입하는 다른 안은 변경 요청이 없어 "왜 바뀌었나" 가 남지 않고, 완료 뒤 다음 단계가 3 단계라 외부 인터페이스부터 다시 밟으라는 안내가 나온다.

### 1.4 push

1. 가장 최근 체크포인트가 완료(DONE)인지 본다 (D4).
2. `git ls-remote` 로 원격 commit 을 보고, `remote.rule.base` 와 다르면 거부한다 (D5). 처음 올리는 빈 저장소는 통과.
3. 임시 디렉토리에 `git clone --depth 1`, 로컬 수칙으로 `rule.kisp` 를 다시 쓰고 (체크포인트 하나로 접는다), commit 메시지 `<프로젝트 이름> CP-xxxx 의 개발 수칙` 으로 commit · push.
4. `remote.rule.base` 를 새 commit 으로. 임시 디렉토리를 지운다.

## 2. 원격과 git 다루는 방법

### 2.1 GitHub 의 대안

| 대안 | 인증 | 비용 | 판단 |
| --- | --- | --- | --- |
| GitHub 공개 저장소 | 읽기 없음, push 만 필요 | 무료 | 권장 |
| GitLab.com · Codeberg · Bitbucket | GitHub 와 같은 git 인증 | 무료 | 얻는 것이 없다 |
| GitHub Gist | push 에 git 인증 | 무료 | 저장소와 같고 기능이 적다 |
| 클라우드 동기화 폴더 (OneDrive · Google Drive · Dropbox) | `ks` 는 인증이 없다. 동기화 프로그램이 한다 | 무료 용량 안 | 가장 단순: `URL` 이 폴더 경로이고 파일 복사만 한다. 대신 commit 이력이 없고, 동시 수정 시 충돌 사본이 생기고, 리눅스 공식 클라이언트가 없는 서비스가 있다 |
| GitHub REST API + 토큰 | 토큰을 `ks` 가 보관 | 무료 | HTTPS 라이브러리와 토큰 관리가 늘어 가장 번거롭다 |
| NAS · 자체 Gitea | 직접 운영 | 장비 | 과하다 |

공개 저장소여도 push 에는 인증이 필요하다. 인증이 필요 없어지는 것은 check · pull 쪽이다.

### 2.2 ssh 키와 `gh` 없이 push 하기

HTTPS URL (`https://github.com/<사용자>/kiotty-rules.git`) 을 쓰면 `git` 이 Git Credential Manager (Git for Windows 에 들어 있음) 를 부른다. 처음 push 할 때 브라우저로 GitHub 로그인을 한 번 하면 OS 자격 증명 저장소에 남고, 그 뒤로는 묻지 않는다. ssh 키도 `gh` 도 필요 없다. macOS 는 키체인, 리눅스는 GCM 을 설치하면 같다.

### 2.3 git 라이브러리(libgit2) 대신 `git` 명령 실행

| | libgit2 를 링크 | `git` 명령 실행 |
| --- | --- | --- |
| 인증 | libgit2 는 git credential helper 를 부르지 않는다. 자격 증명 콜백을 직접 짜야 해서 결국 토큰을 `ks` 가 보관한다 | `git` 이 GCM 을 쓴다. `ks` 는 인증을 모른다 |
| 의존성 | 리눅스 · Android 에서 OpenSSL 이나 mbedTLS, libssh2 까지 빌드 | 사용자 PC 에 git 만 있으면 된다 |
| 개발 수칙 | 런타임 힙을 쓰므로 라이브러리가 아니라 CLI 에만 둘 수 있다 | 같음 (CLI 에 둔다) |
| 오류 | C API 에러 코드 | 종료 코드와 stderr |

권장: `git` 명령 실행. 이 기능에서 git 을 쓰는 일은 `ls-remote`, `clone --depth 1`, `commit`, `push` 네 가지뿐이다. Tauri 플러그인은 Rust `git2` 크레이트가 credential helper 를 지원하므로 그때 따로 고른다.

## 3. 어디에 무엇을 두는가

### 3.1 라이브러리 (C API 추가)

```c
typedef struct KiottySpecRuleDiff {
    char             id[KIOTTY_SPEC_MAX_ID_LENGTH + 1];
    char             title[KIOTTY_SPEC_MAX_TITLE_LENGTH + 1];
    KiottySpecChange change;
    uint32_t         changed_fields;   /* KiottySpecChangedField 비트 */
} KiottySpecRuleDiff;

KiottySpecCursorResult KiottySpec_openRuleDiffCursor(KiottySpecContext *local, KiottySpecContext *remote);
KiottySpecStatusResult KiottySpec_acceptRules(KiottySpecContext *local, KiottySpecContext *remote, KiottySpecText description);
KiottySpecStatusResult KiottySpec_exportRules(KiottySpecContext *local, KiottySpecText path);
KiottySpecStatusResult KiottySpec_readMeta(KiottySpecContext *context, KiottySpecText key, KiottySpecBuffer value);
KiottySpecStatusResult KiottySpec_writeMeta(KiottySpecContext *context, KiottySpecText key, KiottySpecText value);
```

- 받아 둔 `rule.kisp` 는 두 번째 Context 로 연다. `list` · `body` 는 그 Context 에 지금의 `openItemCursor` · `readItemBody` 를 그대로 쓴다.
- `acceptRules` 가 1.3 의 1 ~ 3 을 쓰기 한 번으로 한다. 수칙 75 개를 명령 하나씩 쓰면 쓰기마다 파일을 다시 써서 4 초쯤 걸린다.
- `exportRules` 는 가장 최근 완료 체크포인트의 수칙만 담은 `.kisp` 를 쓴다 (push 의 3).
- 새 struct 는 5.7 을 따른다 (union · bool 없음, 순차 배치, `static_assert(sizeof)`).

### 3.2 CLI (`app/cli`)

- `kiotty_git.cpp`: `git` 실행 (Windows `CreateProcessW`, POSIX `posix_spawnp`), 시간 제한, 종료 코드 · stdout. `git` 이 없으면 "git 을 찾을 수 없음".
- `kiotty_remote_commands.cpp`: 1 장의 명령들.

## 4. 단계

| 단계 | 내용 | 끝났다는 기준 |
| --- | --- | --- |
| R0 | 모델 2: 변경 요청 상태 `백로그`, 출처 `공통 수칙`, model 1 파일 열기 (5 장) | 유닛 테스트: model 1 파일을 열고 다음 쓰기에 머리줄이 model 2, 백로그는 열린 변경 요청으로 세지 않음 |
| R1 | meta 읽기 · 쓰기, `openRuleDiffCursor`, `exportRules` | 유닛 테스트: 같음 · 추가 · 삭제 · 필드별 변경, 완료 체크포인트 기준 내보내기 |
| R2 | `acceptRules` | 유닛 테스트: 조건 세 가지 거부, 부모 순서, 관계가 걸린 삭제, 실패 시 되돌림, 변경 요청과 관계, 진입 단계 min(10, 다음 단계), 낡은 검사 결과 삭제 |
| R3 | git 실행기, `open` · `close` · `push` · `check` · `pull` | 시스템 테스트 (아래) |
| R4 | `list` · `diff` · `body` · `accept` · `discard` | 시스템 테스트 (아래) |
| R5 | 이 저장소에서 첫 push, 다른 프로젝트 하나에서 받기 | 실제 GitHub 로 1.2 전체 |

시스템 테스트는 GitHub 대신 임시 디렉토리의 `git init --bare` 를 원격으로 써서 네트워크 없이 돈다. 시나리오: A push → B check `changed` → B pull · diff · body → B accept → B check `same` → A 다시 수정 · push → B 가 받기 전에 로컬 수정 후 push 하면 거부 → B discard.

## 5. 함께 바꿀 모델: 변경 요청 상태 `백로그`

아이디어만 기록하고 아직 반영하지 않는 변경 요청을 위해, 변경 요청의 `상태` 태그에 `백로그` 를 더한다 (사용자 요청 2026-11-11).

| 상태 | 뜻 | 열린 변경 요청인가 |
| --- | --- | --- |
| 백로그 | 아이디어만 기억한다. 반영 계획 없음 | 아니다 |
| 열림 | 반영하기로 했다 | 그렇다 |
| 반영 중 | 반영하고 있다 | 그렇다 |
| 완료 · 거부 | 끝 | 아니다 |

- 열린 변경 요청은 지금처럼 `열림` · `반영 중` 만이다 (`Checker::isOpenChangeRequest`). 그래서 백로그는 다음 단계 계산, 1 단계 확정 조건(진입 단계 필수), `ks where` 의 열린 변경 요청 수에 들어가지 않는다. 검사 코드는 바꾸지 않는다.
- 진입 단계는 비워 둬도 된다. `열림` 으로 바꾸는 순간 일반 변경 요청이 되어 진입 단계가 필요해진다.
- 값은 목록 끝에 붙인다 (`열림, 반영 중, 완료, 거부, 백로그`). 파일은 비트 이름으로 저장하지만 C API 의 태그 질의는 비트 번호를 쓰므로, 기존 번호를 밀지 않는다.
- 같은 모델 변경에 출처 `공통 수칙` 도 더한다 (`사용자 피드백, 이슈, 초기 요구, 공통 수칙`). accept 가 만드는 변경 요청의 출처다.

모델 버전이 1 에서 2 로 바뀐다. 지금은 머리줄이 `#kisp 2 model 1` 이 아니면 열지 않으므로, 이번 변경은 값 추가뿐이라 model 1 파일도 열고 다음 쓰기에서 머리줄을 model 2 로 쓰게 한다. model 2 파일을 옛 `ks` 가 열면 지금처럼 거부한다. 받아 둔 원격 `rule.kisp` 의 모델 확인(1.3 조건)도 같은 규칙을 쓴다.

## 6. 더 줄일 수 있는 곳 (선택)

지금 목록으로도 충분하다. 줄인다면:

- `check` 를 `pull` 에 합친다. `pull` 이 같으면 `same` 만 찍고 아무것도 받아 두지 않는다. 명령 8 개 → 7 개.
- `list` 를 뺀다. 받기 전에 알고 싶은 것은 다른 수칙뿐이고 그것은 `diff` 가 보여 준다. 받아 둔 수칙 전체는 accept 뒤 `ks item list --kind rule` 과 같다.
- `open URL` · `close` 를 `ks remote set URL` · `ks remote set -` 로. `ks tag set ID 이름 -` 와 같은 모양이다.
