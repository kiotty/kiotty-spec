## 진술
작업 DB 는 프로젝트 루트의 .kiotty/spec.db(SQLite, git 에서 제외), 공유·커밋 형식은 .kiotty/spec/*.jsonl 이다. 모든 문자열은 UTF-8 이며, Windows 에서도 한글 경로·인자·출력이 깨지지 않는다.

## 근거
definition.md 6 절, kiotty_spec.h 머리말 "문자열은 UTF-8", app/cli/main.cpp 의 wmain 과 SetConsoleOutputCP(CP_UTF8).
