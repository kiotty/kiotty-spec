# 개발수칙 편차

개발수칙을 지키지 못하는 곳과 그 이유를 적는다. 항목을 없애면 이 문서에서도 지운다.

| 수칙 | 위치 | 편차 | 이유 | 해소 계획 |
| --- | --- | --- | --- | --- |
| 1.1 런타임 힙 0 | SQLite | 문장 준비, 결과 행, `SQLITE_TRANSIENT` 바인딩 복사에서 내부 힙을 쓴다 | 서드파티 내부 동작 | 보류. `SQLITE_CONFIG_HEAP` 은 프로세스 전역 설정이라 수칙 1.2 · 6.2 와 부딪힌다. 전역 힙을 허용할지, 연결별 lookaside 로 일부만 줄일지 정해야 한다 (2026-11-09) |
| 1.10 Raw pointer | `data/datasource` 의 `DBConnection`, `Statement`, `Row` | `sqlite3*`, `sqlite3_stmt*` 를 멤버로 둔다 | SQLite C API 가 핸들을 포인터로만 준다 | 없음. datasource 밖으로 내보내지 않는다 |
| 1.1 런타임 힙 0 | `app/cli` | `ks` 가 인자와 TSV 줄을 `std::string` · `std::vector` 로 다루고, 루트 찾기에 `std::filesystem`(error_code 오버로드)을 쓴다 | 라이브러리 밖의 presentation 이다 | 없음 |
