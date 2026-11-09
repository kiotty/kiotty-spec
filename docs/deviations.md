# 개발수칙 편차

개발수칙을 지키지 못하는 곳과 그 이유를 적는다. 항목을 없애면 이 문서에서도 지운다.

| 수칙 | 위치 | 편차 | 이유 | 해소 계획 |
| --- | --- | --- | --- | --- |
| 1.1 런타임 힙 0 | SQLite | 문장 준비, 결과 행, `SQLITE_TRANSIENT` 바인딩 복사에서 내부 힙을 쓴다 | 서드파티 내부 동작 | `sqlite3_config(SQLITE_CONFIG_HEAP)` 로 컨텍스트가 확보한 고정 영역을 넘긴다 |
| 1.1 런타임 힙 0 | `domain/usecases/exchange` 의 nlohmann/json | jsonl 가져오기 · 내보내기가 `json` 값, 파일 내용, 변환 보고를 `std::string` · `std::vector` · `std::map` 으로 힙에 만든다 | 서드파티 내부 동작. 파일 하나를 통째로 다룬다 | 줄 단위 고정 버퍼 파서로 바꾼다 |
| 1.1 런타임 힙 0 | `context/context.cpp` 의 `init` | `.gitignore` 내용을 `std::string` 으로 읽고 고친다 | 한 번만 부르는 설정 작업 | 작업 버퍼(`TextBuffer`)로 옮긴다 |
| 1.10 Raw pointer | `data/datasource` 의 `Connection`, `Query`, `Row` | `sqlite3*`, `sqlite3_stmt*` 를 멤버로 둔다 | SQLite C API 가 핸들을 포인터로만 준다 | 없음. datasource 밖으로 내보내지 않는다 |
| 1.1 런타임 힙 0 | `app/cli` | `ks` 가 인자와 TSV 줄을 `std::string` · `std::vector` 로 다루고, 루트 찾기에 `std::filesystem`(error_code 오버로드)을 쓴다 | 라이브러리 밖의 presentation 이다 | 없음 |
