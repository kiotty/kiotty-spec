# C 인터페이스 v2 설계

`include/kiotty_spec.h` 를 개발수칙에 맞게 다시 짠다. 기존 v1 함수와 호환되지 않으며, `ks` 도 함께 바꾼다.

## 1. 확정한 결정

| 결정 | 내용 | 근거 |
| --- | --- | --- |
| enum 이름 | `KIOTTY_SPEC_<TYPE>_<MEMBER>` | 개발수칙 5.2. 적용 완료 |
| 런타임 힙 | 없음. 모든 용량은 `KiottySpecConfiguration` 으로 받아 열 때 확보 | 1.1 |
| 예외 | 없음. 모든 함수가 `KiottySpecStatus` 를 담은 결과를 돌려줌 | 5.1 |
| 본문 | 목록 레코드에 넣지 않는다. 호출자 버퍼로 따로 읽는다 | 1.7, 3.1 |
| 목록 | 커서를 열고, 고정 크기 페이지를 호출자 메모리에 채우고, 닫는다 | 6.6, 5.4 |

## 2. 용량 상한

| 상수 | 값 | 현재 DB 최대 |
| --- | --- | --- |
| `KIOTTY_SPEC_MAX_ID_LENGTH` | 16 | 약 8 |
| `KIOTTY_SPEC_MAX_TITLE_LENGTH` | 256 | 80 |
| `KIOTTY_SPEC_MAX_BODY_LENGTH` | 16384 | 4933 |
| `KIOTTY_SPEC_MAX_DECISION_LENGTH` | 8192 | 1338 |
| `KIOTTY_SPEC_PAGE_CAPACITY` | 64 | 없음 |

- 길이는 UTF-8 바이트 수이며 null 종료 문자를 포함하지 않는다.
- 상한을 넘는 입력은 `KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED` 로 거부한다. 잘라서 저장하지 않는다.

## 3. 형태 규칙

### 3.1 함수 이름

`KiottySpec_<동사><대상>` camelCase 다. 그룹 이름을 밑줄로 끼우지 않는다.

| v1 | v2 |
| --- | --- |
| `KiottySpec_Item_create` | `KiottySpec_createItem` |
| `KiottySpec_Item_listBegin` + `Cursor_getItem` + `Cursor_end` | `KiottySpec_openItemCursor` + `KiottySpec_fetchItemPage` + `KiottySpec_closeCursor` |
| `KiottySpec_Checkpoint_confirm` | `KiottySpec_confirmCheckpoint` |
| `KiottySpec_Context_whereAmI` | `KiottySpec_readPosition` |

### 3.2 입력 문자열

null 종료를 가정하지 않는다(2.6). 모든 문자열 입력은 길이를 함께 받는다.

```c
typedef struct KiottySpecText {
    const char *data;
    size_t length;
} KiottySpecText;
```

### 3.3 결과

출력 파라미터 대신 결과 구조체를 돌려준다(4.6). 구조체는 1024 바이트 이하다(1.7).

```c
typedef struct KiottySpecStatusResult  { KiottySpecStatus code; } KiottySpecStatusResult;
typedef struct KiottySpecSizeResult    { KiottySpecStatus code; size_t value; } KiottySpecSizeResult;
typedef struct KiottySpecBoolResult    { KiottySpecStatus code; bool value; } KiottySpecBoolResult;
typedef struct KiottySpecIdResult      { KiottySpecStatus code; char value[KIOTTY_SPEC_MAX_ID_LENGTH + 1]; } KiottySpecIdResult;
typedef struct KiottySpecCursorResult  { KiottySpecStatus code; KiottySpecCursor value; size_t total; size_t page_count; } KiottySpecCursorResult;
```

### 3.4 레코드

포인터 없이 고정 배열로 담는다(1.4). 필드 이름은 snake_case 다(4.5).

```c
typedef struct KiottySpecItem {
    char id[KIOTTY_SPEC_MAX_ID_LENGTH + 1];
    char parent_id[KIOTTY_SPEC_MAX_ID_LENGTH + 1];
    char checkpoint_from[KIOTTY_SPEC_MAX_ID_LENGTH + 1];
    char title[KIOTTY_SPEC_MAX_TITLE_LENGTH + 1];
    KiottySpecItemKind kind;
    KiottySpecItemCategory category;
    int32_t sequence;
    size_t body_length;
} KiottySpecItem;

typedef struct KiottySpecItemPage {
    size_t length;
    KiottySpecItem elements[KIOTTY_SPEC_PAGE_CAPACITY];
} KiottySpecItemPage;
```

### 3.5 컨텍스트와 핸들

외부에는 컨텍스트와 핸들만 보인다(2.5). 커서는 generation 이 든 핸들이다(1.3).

```c
typedef struct KiottySpecContext KiottySpecContext;
typedef uint64_t KiottySpecCursor;

typedef struct KiottySpecConfiguration {
    KiottySpecText project_root;
    size_t max_cursors;
    size_t max_cursor_rows;
} KiottySpecConfiguration;

typedef struct KiottySpecContextResult { KiottySpecStatus code; KiottySpecContext *value; } KiottySpecContextResult;

KiottySpecContextResult KiottySpec_createContext(const KiottySpecConfiguration *configuration);
void                    KiottySpec_releaseContext(KiottySpecContext *context);
```

- `createContext` 가 커서 풀, 페이지 작업 버퍼, 에러 메시지 버퍼를 모두 확보한다. 이후 힙 할당은 없다.
- `releaseContext` 하나로 모든 커서와 버퍼가 정리된다(1.2).

### 3.6 큰 텍스트 출력

본문, 결정 기록, 보고서, 맥락 문서는 호출자 버퍼로 읽는다(5.4).

```c
KiottySpecSizeResult KiottySpec_readItemBody(KiottySpecContext *context, KiottySpecText item_id, char *buffer, size_t capacity);
```

- 성공하면 `value` 는 쓴 바이트 수이고, 버퍼는 null 종료된다.
- 버퍼가 작으면 `CAPACITY_EXCEEDED` 를 돌려주고 `value` 에 필요한 바이트 수를 담는다. 버퍼에는 쓰지 않는다.

### 3.7 에러 메시지

스레드 지역 `std::string` 대신 컨텍스트 안의 고정 버퍼를 쓴다.

```c
KiottySpecSizeResult KiottySpec_readLastError(KiottySpecContext *context, char *buffer, size_t capacity);
```

## 4. 구현 순서

각 단계는 `ks` 기준 출력 비교와 jsonl 내보내기 비교를 통과해야 다음으로 넘어간다.

1. `data/datasource` 의 DB 래퍼를 `Result` 반환으로 바꾼다. 위 계층은 임시 변환 함수로 기존 흐름을 유지한다.
2. `data/repository` 를 고정 크기 레코드와 `Result` 로 바꾼다.
3. `domain/usecases` 를 바꾼다.
4. `context/` 에 컨텍스트, 커서 풀, 작업 버퍼를 만든다.
5. 공개 헤더를 v2 로 교체하고 `src/kiotty_spec.cpp` 를 다시 쓴다.
6. `ks` 를 v2 에 맞춘다. 예외와 임시 변환 함수를 지우고 `-fno-exceptions` 로 빌드한다.

## 5. 남은 위험

- `std::regex` 와 `std::filesystem` 은 예외를 던진다. 1단계 전에 대체 구현이 필요하다.
- SQLite 와 nlohmann/json 은 내부에서 힙을 쓴다. 개발수칙 1.1 편차로 `docs/deviations.md` 에 기록해야 한다.
