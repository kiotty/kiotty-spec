## 규칙
모든 자체 타깃은 kiotty_warnings() 로 MSVC /W4 /utf-8 /permissive- /Zc:__cplusplus, GCC·Clang -Wall -Wextra -Wpedantic 로 빌드하고 경고 0 을 유지한다. 외부 라이브러리 타깃에는 적용하지 않는다.

## 이유
이식성 문제와 실수를 컴파일 단계에서 잡는다.

## 검사 방법
Release 빌드 로그에서 자체 소스(src/, app/)의 warning 수 0 확인
