# 개발수칙 편차

개발수칙을 지키지 못하는 곳과 그 이유를 적는다. 항목을 없애면 이 문서에서도 지운다.

| 수칙 | 위치 | 편차 | 이유 | 해소 계획 |
| --- | --- | --- | --- | --- |
| 1.1 런타임 힙 0 | SQLite | 문장 준비, 결과 행, `SQLITE_TRANSIENT` 바인딩 복사에서 내부 힙을 쓴다 | 서드파티 내부 동작 | `sqlite3_config(SQLITE_CONFIG_HEAP)` 로 컨텍스트가 확보한 고정 영역을 넘긴다 |
| 1.1 런타임 힙 0 | nlohmann/json | jsonl 가져오기·내보내기와 `ks query` 가 `json` 값을 힙에 만든다 | 서드파티 내부 동작 | 줄 단위 고정 버퍼 파서로 바꾼다 |
| 1.1, 1.6 고정 크기 컨테이너 | `data/repository` 의 레코드와 목록 | 레코드가 `std::string`, 목록이 `std::vector`·`std::set` 이다 | 레코드를 usecase·C 경계가 함께 쓴다 | 3단계에서 레코드를 고정 크기로 바꾸고, 5단계에서 목록을 커서 페이지로 바꾼다 |
| 1.10 Raw pointer | `data/datasource` 의 `Connection`, `Query`, `Row` | `sqlite3*`, `sqlite3_stmt*` 를 멤버로 둔다 | SQLite C API 가 핸들을 포인터로만 준다 | 없음. datasource 밖으로 내보내지 않는다 |
| 4.1 public 메서드 7 개 | `ItemStore`, `LinkStore`, `CheckpointStore` | public 메서드가 11·14·10 개다 | 2단계는 반환 형태만 바꿨다 | 3단계에서 탐색·판정 메서드를 usecase 로 옮긴다 |
| 5.1 예외 금지 | `data/datasource/database.*` 의 `Database`, `Statement`, `LegacySavepoint` | `Result` 를 예외로 바꿔 던진다 | `exchange`, `milestone_service` 가 아직 쓴다 | 3단계에서 두 usecase 가 `Storage` 를 쓰면 지운다 |
| 5.1 예외 금지 | `core/error.h` 의 `expect` | usecase·context·C 경계가 repository 의 `Result` 를 예외로 바꾼다 | 위 계층은 아직 예외로 흐름을 잇는다 | 3~5단계에서 계층마다 `Result` 를 돌려주게 바꾸고 6단계에서 지운다 |
| 5.1 예외 금지 | `std::filesystem` | 일부 함수가 실패 시 예외를 던진다 | 표준 라이브러리 동작 | 3단계에서 고정 버퍼 대체 구현으로 바꾼다 |
