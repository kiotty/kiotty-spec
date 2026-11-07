# 개발수칙 편차

개발수칙을 지키지 못하는 곳과 그 이유를 적는다. 항목을 없애면 이 문서에서도 지운다.

| 수칙 | 위치 | 편차 | 이유 | 해소 계획 |
| --- | --- | --- | --- | --- |
| 1.1 런타임 힙 0 | SQLite | 문장 준비, 결과 행, `SQLITE_TRANSIENT` 바인딩 복사에서 내부 힙을 쓴다 | 서드파티 내부 동작 | `sqlite3_config(SQLITE_CONFIG_HEAP)` 로 컨텍스트가 확보한 고정 영역을 넘긴다 |
| 1.1 런타임 힙 0 | nlohmann/json | jsonl 가져오기·내보내기와 `ks query` 가 `json` 값을 힙에 만든다 | 서드파티 내부 동작 | 줄 단위 고정 버퍼 파서로 바꾼다 |
| 1.1, 1.6 고정 크기 컨테이너 | `domain/entity/records.h` 의 긴 본문 | 항목 본문, 변경 요청 본문, 결과 로그가 `std::string` 이다 | v1 C API 가 레코드 안에 본문을 넣어 돌려준다 | 5단계에서 본문을 레코드에서 빼고 호출자 버퍼로 읽는다 |
| 1.1, 1.6 고정 크기 컨테이너 | repository·usecase 의 목록과 보고서 | 목록이 `std::vector`·`std::set`, 보고서와 ID 목록이 `std::string` 이다. `FixedString` 의 `operator+` 도 `std::string` 을 만든다 | 위 계층이 아직 문자열을 이어 붙여 문서를 만든다 | 4단계에서 작업 버퍼, 5단계에서 커서 페이지로 바꾼다 |
| 1.10 Raw pointer | `data/datasource` 의 `Connection`, `Query`, `Row` | `sqlite3*`, `sqlite3_stmt*` 를 멤버로 둔다 | SQLite C API 가 핸들을 포인터로만 준다 | 없음. datasource 밖으로 내보내지 않는다 |
| 4.1 public 메서드 7 개 | `context/` 의 `ContextBuilder`, `Project` | `ContextBuilder` 가 8 개, `Project` 가 store 를 public 멤버로 내놓는다 | 3단계는 usecase 까지만 바꿨다 | 4단계에서 컨텍스트를 다시 짠다 |
| 5.1 예외 금지 | `core/error.h` 의 `expect`, `fail`, `require` | context 와 C 경계가 `Result` 를 예외로 바꾼다 | 위 계층은 아직 예외로 흐름을 잇는다 | 4~5단계에서 계층마다 `Result` 를 돌려주게 바꾸고 6단계에서 지운다 |
| 5.1 예외 금지 | `app/cli` 의 `std::filesystem` | CLI 가 경로 처리에 `std::filesystem` 을 쓴다 | 라이브러리 밖이다 | 6단계에서 `ks` 를 v2 에 맞출 때 바꾼다 |
