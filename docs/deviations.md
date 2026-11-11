# 개발수칙 편차

개발수칙이나 참조 표준(MISRA C++:2023, CERT C++)을 지키지 못하는 곳과 그 이유, 위험을 줄이는 방법을 적는다. 편차를 없애면 이 문서에서도 지운다.

| 수칙 | 위치 | 편차 | 이유 | 완화책 |
| --- | --- | --- | --- | --- |
| MISRA C++:2023 21.6.1 (동적 메모리 금지) | `KiottySpec_createContext`, 풀 · 버퍼를 가진 클래스의 생성자 | 초기화 때 `std::vector`, `std::make_unique` 로 힙을 할당한다 | 용량을 설정값(`KiottySpecConfiguration`)으로 정해야 해서 컴파일 타임 배열로 둘 수 없다 (개발수칙 1.1) | 할당은 초기화 함수 안에서 한 번만 하고 그 뒤 크기를 바꾸지 않는다. 설정값은 `parseConfiguration` 이 범위를 검사하고, 곱한 용량은 `checkedMultiply` 로 넘침을 막는다. 할당 실패는 `-fno-exceptions` 로 process 를 끝낸다. `tools/lint.js` 가 초기화 함수 밖의 할당을 1.1 위반으로 센다 |
| 개발수칙 1.1 (힙 할당은 초기화 함수 안에서만) | `app/` (CLI) | 명령 실행 중에 `std::string`, `std::vector`, `std::map`, 표준 입출력으로 힙을 할당한다 | CLI 는 한 번 실행하고 끝나는 짧은 process 라 런타임 힙이 쌓이지 않고, 막으면 인자 · 출력 처리 비용이 지나치게 크다 | 라이브러리(`src/`)는 그대로 1.1 을 지킨다. CLI 는 C interface 만 거쳐 라이브러리를 쓴다. `tools/lint.js` 는 `app/` 의 1.1 을 세지 않는다 |
| 개발수칙 1.10 (Raw pointer 금지) | `include/`, C interface 어댑터(`src/kiotty_spec*.cpp`), CLI 에서 C 함수(`KiottySpec_*`, C 표준 라이브러리, Win32)를 부르거나 `KiottySpec*` 레코드를 다루는 줄 | C ABI 의 인자 · 레코드에 raw pointer 를 쓴다 | C 에는 길이와 포인터 말고 버퍼를 넘길 방법이 없다 | 포인터는 항상 길이와 함께 다니고(2.2), 어댑터가 `Span` 으로 바꿔 내부로 넘긴다. 경계 밖의 내부 코드(CLI 자체 포함)는 1.10 을 지킨다. `tools/lint.js` 는 이 줄들만 1.10 에서 뺀다 |
