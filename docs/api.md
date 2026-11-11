# Kiotty-Spec C 인터페이스 설명

`include/kiotty_spec.h` 의 설명이다. 개발수칙 4.8 에 따라 소스에는 Release 전까지 주석을 두지 않으므로, 헤더의 각 선언이 무엇을 하는지는 이 문서에 적는다.

## 형태 규칙

- 문자열 입력은 길이가 붙은 `KiottySpecText {data, length}` 다. null 종료를 가정하지 않는다.
  - 모든 문자열 입력은 경계에서 검사한다. `data` 가 NULL 인데 `length > 0` 이거나 중간에 NUL 바이트가 있으면 `INVALID_ARGUMENT`, 상한보다 길면 `CAPACITY_EXCEEDED`, UTF-8 이 아니면 `INVALID_UTF8` 이다.
- 결과는 상태 코드 `code` 를 담은 결과 구조체다. 실패 이유는 `KiottySpec_readLastError` 로 읽는다.
- 레코드는 포인터가 없는 고정 크기 구조체다. 구조체 멤버는 크기 내림차순으로 놓는다 (개발수칙 3.4).
- 라이브러리는 밖으로 포인터를 내보내지 않는다. 밖에서 갖는 것은 `KiottySpecContext` 와 Handle 뿐이다 (개발수칙 2.5).
- 여러 레코드는 커서를 열고 (`open*Cursor`) 고정 크기 페이지를 받아 오고 (`fetch*Page`) 닫는다 (`KiottySpec_closeCursor`). 커서 결과의 `total` 은 행 수다.
- 본문 · 결정 기록 같은 큰 텍스트는 호출자 버퍼 `KiottySpecBuffer {data, capacity}` 로 읽는다.
- 16 바이트보다 큰 입력 구조체는 `const` 포인터로 넘긴다 (`KiottySpecItemDraft`, `KiottySpecDiffQuery`).
- 문장은 라이브러리가 만들지 않는다. 에러와 확정 조건은 코드로 돌려주고, 문장은 호출자(`ks`)가 만든다 (개발수칙 5.5, 8).

## 용량 상수

길이는 null 종료 문자를 뺀 UTF-8 바이트 수다.

| 상수 | 값 | 뜻 |
| --- | --- | --- |
| `KIOTTY_SPEC_MAX_ID_LENGTH` | 15 | 항목 ID |
| `KIOTTY_SPEC_MAX_TITLE_LENGTH` | 127 | 제목 |
| `KIOTTY_SPEC_MAX_SUMMARY_LENGTH` | 255 | 요약 |
| `KIOTTY_SPEC_MAX_BODY_LENGTH` | 16384 | 본문 |
| `KIOTTY_SPEC_MAX_DECISION_LENGTH` | 2048 | 결정 기록 |
| `KIOTTY_SPEC_MAX_DESCRIPTION_LENGTH` | 63 | 체크포인트 설명 |
| `KIOTTY_SPEC_MAX_PROJECT_NAME_LENGTH` | 255 | 프로젝트 이름 |
| `KIOTTY_SPEC_MAX_NAME_LENGTH` | 31 | `format*` 이 돌려주는 이름 |
| `KIOTTY_SPEC_MAX_ERROR_TEXT_LENGTH` | 255 | 에러의 `text` |
| `KIOTTY_SPEC_PAGE_CAPACITY` | 64 | 한 페이지의 원소 수 |
| `KIOTTY_SPEC_CHECKPOINT_CURRENT` | 0 | "가장 최근 체크포인트" |

## 컨텍스트

- `KiottySpec_createContext(configuration)`: 힙 할당은 여기서만 한다. 풀 · 커서 · 버퍼를 전부 확보한다. 설정값 0 은 기본값이다.
  - 범위: 개수(`max_items`, `max_relations`, `max_tags`, `max_checkpoints`, `max_cursor_rows`)는 1 ~ `KIOTTY_SPEC_MAX_POOL_CAPACITY`, `max_cursors` 는 1 ~ `KIOTTY_SPEC_MAX_CURSOR_CAPACITY`, `work_buffer_bytes` 는 `KIOTTY_SPEC_MIN_WORK_BUFFER_BYTES` ~ `KIOTTY_SPEC_MAX_BUFFER_BYTES`, `max_body_bytes` 는 `KIOTTY_SPEC_MAX_BUFFER_BYTES` 이하. 벗어나면 `INVALID_ARGUMENT`.
  - `max_body_bytes` 는 메모리에 두는 현재 본문 전체의 바이트 수다.
- `KiottySpec_releaseContext`: 컨텍스트와 그것이 가진 모든 것을 정리한다.
- `KiottySpec_open(context, path)`: `.kisp` 파일을 연다. 없으면 디렉토리와 함께 빈 파일을 만들고, 같은 디렉토리에 `.gitattributes` (`*.kisp text eol=lf merge=union`) 가 없으면 만든다. 컨텍스트 하나는 파일 하나만 연다.
  - `.kisp` 는 사람이 읽고 git 이 병합하는 텍스트이며 쓰기마다 전부 다시 쓴다. 항목의 현재 본문도 `.kisp` 의 `body` 줄에 있다.
  - 연 뒤에 파일이 밖에서 바뀌면 다음 쓰기는 `CONFLICT` (subject `FILE_CHANGED`) 이고 다시 읽는다.
- `KiottySpec_initializeProject(context, project_name)`: 프로젝트 이름과 만든 시각을 남긴다. 이름 길이가 0 이면 프로젝트 루트(`.kiotty` 의 상위) 디렉토리 이름이다.

## 에러

`KiottySpec_readLastError(context)` 는 마지막 실패를 `KiottySpecError` 로 돌려준다. 아직 실패가 없으면 `status = SUCCESS`, `subject = NONE` 이다.

| 필드 | 뜻 |
| --- | --- |
| `status` | 실패한 호출이 돌려준 상태 코드 |
| `subject` | 실패가 무엇에 관한 것인지 |
| `text` | 관련 값 하나. 항목 ID, 경로, 표 이름, 설정 이름, 태그 · 관계 이름 중 하나이며 문장이 아니다 |
| `line` | `.kisp` 의 줄 번호. 줄과 상관없으면 0 |

| subject | 뜻 | `text` |
| --- | --- | --- |
| `FILE` | `.kisp` 경로나 파일 · 디렉토리를 열기 · 읽기 · 쓰기 · 바꾸기 실패 | 경로 |
| `FILE_LINE` | 형식이 잘못된 줄 | 표 이름 |
| `FILE_FORMAT` | 첫 줄이 기대한 형식 · 모델 버전이 아님 | |
| `MERGE_MARKER` | 병합 충돌 표시가 남은 줄 | |
| `DUPLICATE_LINE` | 같은 레코드 · ID · 본문이 두 줄 | 키나 ID |
| `FILE_CHANGED` | 연 뒤에 밖에서 바뀜 | 경로 |
| `CONFIGURATION` | 설정이 정한 용량이 모자람 | 설정 이름 (`max_items` 등) |
| `MODEL` | 모델 표를 적재할 수 없음 | |
| `KIND` | 알 수 없는 Kind, 결정 기록이 없는 Kind | |
| `ITEM` | 없는 항목, 그 시점에 없는 항목, 무효한 핸들 | ID |
| `ID` | ID 형식이 잘못됨, 이미 있는 ID | ID |
| `ID_IN_HISTORY` | 지운 항목의 이력이 쓰는 ID (정리 전까지 쓸 수 없음) | ID |
| `TITLE` · `SUMMARY` · `BODY` | 제목 · 요약 · 본문이 비었거나 길거나 UTF-8 이 아님 | |
| `PAST_BODY` | 그 시점의 본문은 지금과 달라 git 이력에만 있음 | ID |
| `PARENT` | 상위 항목이 순환함 | ID |
| `CHILD` | 하위 항목이 있어 지울 수 없음 | 하위 항목 ID |
| `TAG` · `TAG_VALUE` | 모델에 없는 태그, 태그 정의에 맞지 않는 값 | 태그 이름이나 ID |
| `RELATION` | 모델에 없는 관계, 이미 있는 관계, 없는 관계 | 관계 이름 |
| `DECISION` | 결정 기록이 길거나 UTF-8 이 아님 | |
| `CHECKPOINT` | 체크포인트가 없음, 가장 최근이 DONE, 기준 체크포인트가 없거나 DONE 이 아님 | |
| `DESCRIPTION` | 체크포인트 설명이 길거나 UTF-8 이 아님 | |
| `STEP` | 단계가 1 ~ 12 가 아님 | |
| `ACTOR` | 사용자만 할 수 있음 | |
| `CURSOR` | 커서 수가 `max_cursors` 에 닿음 | |
| `CURSOR_ROWS` | 결과 행이 `max_cursor_rows` 를 넘음 | |
| `DIFF_RANGE` | 비교 구간이 거꾸로이거나 비었거나 시작이 체크포인트가 아님 | |
| `TEXT_FIELD` | 글자 diff 는 제목이나 요약 하나에만 쓴다 | |
| `PROJECT_NAME` | 프로젝트 이름이 길거나 UTF-8 이 아님 | |
| `COMPLETION` | 확정 조건이 어긋남. 어긋난 항목은 `openUnmetConditionCursor` 로 읽는다 | |

## 이름

- `KiottySpec_formatStatus`, `KiottySpec_formatKind`, `KiottySpec_formatCheckpointState`: 이름을 `value.value` 에 복사한다. 모르는 값은 `INVALID_ARGUMENT` 와 빈 이름이다.
- Kind 이름은 `.kisp` 와 `ks` 가 쓰는 영문 snake_case 다 (`"design"`). 한글 이름은 `readModel` 의 `name` 이다.
- `KiottySpec_parseKind(name)`: 모르는 이름이면 0.

## 항목

- `KiottySpec_createItem(context, draft)`: `draft->id` 길이가 0 이면 Kind 접두어로 다음 번호를 발급한다 (예: `DSN-007`). 이미 있는 ID 는 지운 항목의 것이라도 `CONFLICT` 다.
- `KiottySpec_renameItem(context, id, new_id)`: ID 만 바꾼다. 다른 기록은 ID 가 아니라 내부 키로 항목을 가리키므로 함께 고칠 것이 없다. 체크포인트에 묶이지 않고 지난 시점도 새 ID 로 보인다.
- `KiottySpec_readItem`, `KiottySpec_readItemByHandle`, `KiottySpec_findItem`: 현재 항목을 읽는다.
- `KiottySpec_updateItem(context, id, title, summary)`: `data` 가 NULL 인 입력은 바꾸지 않는다.
- `KiottySpec_setItemParent(context, id, parent_id)`: `parent_id` 길이가 0 이면 상위 항목을 없앤다.
- `KiottySpec_removeItem`: 하위 항목이 있으면 `PRECONDITION` (subject `CHILD`).

### 본문

- `KiottySpec_writeItemBody(context, id, text)`
- `KiottySpec_readItemBody(context, id, buffer)`: 값은 본문 길이다.

### 태그

- `KiottySpec_setItemTag(context, item, tag, value)`: 0 을 쓰면 레코드를 지운다.
- `KiottySpec_readItemTag`: 레코드가 없으면 `value = 0` 으로 `SUCCESS`.
- `KiottySpec_openTagCursor` / `KiottySpec_fetchTagPage`

### 관계

관계는 아래 단계가 위 단계를 가리킨다. `UPSTREAM` 은 이 항목이 주어(from)인 관계, `DOWNSTREAM` 은 목적어(to)인 관계다.

- `KiottySpec_addRelation`, `KiottySpec_removeRelation`
- `KiottySpec_openRelationCursor(context, id, direction)`, `KiottySpec_openRelationCursorByModel(context, kind, relation)`, `KiottySpec_fetchRelationPage`

### 목록

- `KiottySpec_openItemCursor(context, kind, tag_queries, count)`: `tag_queries` 는 0 ~ `KIOTTY_SPEC_MAX_TAG_QUERIES` 개이며 AND 다. kind 가 0 이면 모든 Kind. 항목이 고르는 조건은 `TagRecord(model == kind, tag == tag_index)` 가 있고 `(value & mask) != 0` 이다.
- `KiottySpec_openChildCursor(context, id)`
- `KiottySpec_openImpactCursor(context, id, depth)`: 이 항목을 (깊이 `depth` 까지) 가리키는 하류 항목. 결과는 ID 페이지다.
- `KiottySpec_fetchItemPage`, `KiottySpec_fetchIdPage`, `KiottySpec_closeCursor`

## 체크포인트

쓰기는 가장 최근 체크포인트에 묶이며, 그것이 DONE 이면 `CONFIRMED` 다.

- `KiottySpec_startCheckpoint(context, step, description)`
- `KiottySpec_resumeCheckpoint(context)`
- `KiottySpec_completeCheckpoint(context, actor)`: 확정 조건을 검사하고 DONE 으로 바꾼다. actor 가 USER 가 아니면 `PERMISSION`, 조건이 어긋나면 `PRECONDITION` (subject `COMPLETION`).
- `KiottySpec_openUnmetConditionCursor(context)`: 가장 최근 체크포인트의 단계로 확정 조건을 검사하고, 어긋난 것을 `{condition, id}` 행으로 담는다. `KiottySpec_fetchUnmetConditionPage` 로 읽는다.

| condition | 어긋난 것 | `id` |
| --- | --- | --- |
| `ENTRY_STEP` | 진입 단계가 정해지지 않은 열린 변경 요청 | 변경 요청 |
| `ACCEPTANCE_CRITERIA` | 인수 조건이 없는 기능 요구사항 | 요구사항 |
| `MILESTONE_COVERAGE` | 어떤 마일스톤에도 포함되지 않은 설계 사항 · 유닛 테스트 | 설계 · 유닛 테스트 |
| `SYSTEM_TEST_PAIR` | 시스템 테스트와 1:1 이 아닌 기능 요구사항 | 요구사항 |
| `END_TO_END_TEST_PAIR` | E2E 테스트와 1:1 이 아닌 외부 인터페이스 | 외부 인터페이스 |
| `CODE_INSPECTION_PAIR` | 코드 검사 결과와 1:1 이 아닌 개발 수칙 | 개발 수칙 |
| `SINGLE_TARGET` | 대상이 하나가 아닌 테스트 · 검사 항목 | 테스트 · 검사 항목 |
| `PASSING_RESULT` | PASS 가 아닌 결과 | 결과 항목 |
| `CLOSED_CHANGE_REQUEST` | 열린 변경 요청 | 변경 요청 |

- `KiottySpec_readCheckpoint(context, handle)`: handle 이 `KIOTTY_SPEC_CHECKPOINT_CURRENT` 이면 가장 최근 체크포인트.
- `KiottySpec_openCheckpointCursor(context, step)`: step 이 0 이면 모든 단계. `KiottySpec_fetchCheckpointPage`.
- `KiottySpec_pruneCheckpoints(context, actor, base)`: base 이전의 체크포인트와 거기에만 속한 버전을 지운다. 값은 지운 체크포인트 수.

## 결정 기록

결정 기록이 있는 Kind: 요구사항, 개발 수칙, 외부 인터페이스, 설계 사항, 유닛 테스트, 마일스톤.

- `KiottySpec_writeDecision(context, kind, text)`
- `KiottySpec_readDecision(context, kind, buffer)`

## 모델

- `KiottySpec_readModel(context, kind)`
- `KiottySpec_readTagDefinition(context, kind, index, definition)`: `KiottySpecTagDefinition` 이 커서 결과 구조체에 담기에 커서 호출자 메모리에 채운다.
- `KiottySpec_readRelationDefinition(context, kind, index)`

## 위치

- `KiottySpec_readPosition(context)`: 다시 돌아왔을 때 볼 위치. 체크포인트가 없으면 `current`, `step`, `state` 가 0. `next_step` 은 다음에 할 단계이며 끝났으면 0. `pending_writes` 는 `current` 가 RUNNING 일 때 거기에 묶인 쓰기 수.
- `KiottySpec_openStepRunCursor` / `KiottySpec_fetchStepRunPage`: 같은 단계가 이어진 체크포인트 구간. `state` 는 `last` 의 상태다.

## 체크포인트 사이 비교

`KiottySpecDiffQuery` 의 `from < to` 여야 하며 `to` 가 `KIOTTY_SPEC_CHECKPOINT_CURRENT` 이면 가장 최근 체크포인트다. 거꾸로 된 구간은 `INVALID_ARGUMENT`, 정리로 지워진 체크포인트는 `NOT_FOUND` 다. `kind_mask` 는 비트 (kind - 1) 이며 0 이면 모든 Kind, `field_mask` 는 `KiottySpecChangedField` 이며 0 이면 거르지 않는다.

- `KiottySpec_summarizeDiff(context, query)`: Kind 별 추가 · 수정 · 삭제 수와 관계 · 결정 기록 변경. `to` 는 CURRENT 를 실제 체크포인트로 바꾼 값이다. `kinds` 의 index 는 kind - 1.
- `KiottySpec_openItemDiffCursor` / `KiottySpec_fetchItemDiffPage`: Kind 오름차순, 그다음 ID 오름차순.
  - `before` 는 from 시점 버전의 체크포인트(추가면 0), `after` 는 to 시점 버전의 체크포인트(삭제면 지운 체크포인트), `item` 은 지금 풀에 있으면 그 핸들, 없으면 INVALID. `changed_fields` 는 수정일 때만.
- `KiottySpec_openRelationDiffCursor` / `KiottySpec_fetchRelationDiffPage`: Kind, from_id, relation, to_id 순. `field_mask` 는 쓰지 않는다. `change` 는 ADDED 또는 REMOVED.
- `KiottySpec_readItemAt(context, id, checkpoint)`: 그 시점의 버전. 지워진 항목도 그 시점에 있었으면 읽는다. 그 시점에 없거나 지워져 있으면 `NOT_FOUND`. handle 은 지금 풀에 있으면 그 핸들, 없으면 INVALID. parent 는 지금 풀에 있는 상위 항목이면 그 핸들.
- `KiottySpec_readItemTagAt(context, id, tag, checkpoint)`
- `KiottySpec_readItemBodyAt(context, id, checkpoint, buffer)`: 본문은 현재 것만 `.kisp` 에 있다. 그 시점의 본문이 지금과 같을 때만 읽고, 다르면 `NOT_FOUND` (subject `PAST_BODY`, 과거 본문은 git 이력에 있다).
- `KiottySpec_readDecisionAt(context, kind, checkpoint, buffer)`
- `KiottySpec_openTextDiffCursor(context, item, field, range)`: 제목 · 요약의 글자 diff. `range` 의 `from` · `to` 만 쓴다. `total` 은 조각 수. 본문(BODY)이나 비트 둘 이상은 `INVALID_ARGUMENT` (subject `TEXT_FIELD`).
- `KiottySpec_fetchTextDiffPage`: 조각마다 글자 `text`, 바이트 수 `length`, `change` (UNCHANGED · ADDED · REMOVED). 다 읽은 커서는 `KiottySpec_closeCursor` 로 닫는다.
