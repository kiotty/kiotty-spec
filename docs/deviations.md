# 개발수칙 편차

개발수칙이나 참조 표준(MISRA C++:2023, CERT C++)을 지키지 못하는 곳과 그 이유, 위험을 줄이는 방법을 적는다. 편차를 없애면 이 문서에서도 지운다.

| 수칙 | 위치 | 편차 | 이유 | 완화책 |
| --- | --- | --- | --- | --- |
| MISRA C++:2023 21.6.1 (동적 메모리 금지) | `KiottySpec_createContext`, 풀 · 버퍼를 가진 클래스의 생성자 | 초기화 때 `std::vector`, `std::make_unique` 로 힙을 할당한다 | 용량을 설정값(`KiottySpecConfiguration`)으로 정해야 해서 컴파일 타임 배열로 둘 수 없다 (개발수칙 1.1) | 할당은 초기화 함수 안에서 한 번만 하고 그 뒤 크기를 바꾸지 않는다. 설정값은 `parseConfiguration` 이 범위를 검사하고, 곱한 용량은 `checkedMultiply` 로 넘침을 막는다. 할당 실패는 `-fno-exceptions` 로 process 를 끝낸다. `tools/lint.js` 가 초기화 함수 밖의 할당을 1.1 위반으로 센다 |
