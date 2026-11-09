cmake_minimum_required(VERSION 3.24)

project(kiotty_spec VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC 기본 플래그의 /EHsc 를 빼고 타깃마다 kiotty_no_exceptions 로 정한다.
if(MSVC)
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

include(FetchContent)

# ---------------------------------------------------------------------------
# 외부 의존성: SQLite(amalgamation), nlohmann/json(헤더 전용)
# 두 프로젝트 모두 자체 CMake 를 쓰지 않고 소스만 받아 아래에서 직접 타깃을 만든다.
# ---------------------------------------------------------------------------
FetchContent_Declare(sqlite3
    URL https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR _not_a_cmake_project)
FetchContent_MakeAvailable(sqlite3 nlohmann_json)

add_library(kiotty_sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
target_include_directories(kiotty_sqlite3 PUBLIC ${sqlite3_SOURCE_DIR})
target_compile_definitions(kiotty_sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0)

add_library(kiotty_json INTERFACE)
target_include_directories(kiotty_json INTERFACE ${nlohmann_json_SOURCE_DIR}/include)

# ---------------------------------------------------------------------------
# 공통 컴파일 옵션
# ---------------------------------------------------------------------------
function(kiotty_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8 /permissive- /Zc:__cplusplus)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# 예외 없이 빌드한다 (개발수칙 5.1). nlohmann/json 은 JSON_NOEXCEPTION 으로 쓴다.
function(kiotty_no_exceptions target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /EHs-c-)
        target_compile_definitions(${target} PRIVATE _HAS_EXCEPTIONS=0)
    else()
        target_compile_options(${target} PRIVATE -fno-exceptions)
    endif()
    target_compile_definitions(${target} PRIVATE JSON_NOEXCEPTION)
endfunction()

# ---------------------------------------------------------------------------
# 라이브러리: include/kiotty_spec.h 의 C 인터페이스 구현
# ---------------------------------------------------------------------------
file(GLOB_RECURSE KIOTTY_IMPL_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/implementation/*.cpp)

add_library(kiotty_spec STATIC
    src/kiotty_spec.cpp
    ${KIOTTY_IMPL_SOURCES})
target_include_directories(kiotty_spec
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(kiotty_spec PRIVATE kiotty_sqlite3 kiotty_json)
kiotty_warnings(kiotty_spec)
kiotty_no_exceptions(kiotty_spec)

# ---------------------------------------------------------------------------
# CLI: AI 가 질의하는 실행 파일 (이름: ks)
# ---------------------------------------------------------------------------
file(GLOB KIOTTY_CLI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/app/cli/*.cpp)
add_executable(ks ${KIOTTY_CLI_SOURCES})
target_link_libraries(ks PRIVATE kiotty_spec)
kiotty_warnings(ks)
kiotty_no_exceptions(ks)

# ---------------------------------------------------------------------------
# core 단위 테스트: 개발수칙 0.1 에 따라 C++17 로 빌드한다.
# ---------------------------------------------------------------------------
add_executable(kiotty_core_test tests/core/core_test.cpp src/implementation/core/kiotty_text_diff.cpp)
target_include_directories(kiotty_core_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src/implementation/core)
set_target_properties(kiotty_core_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
kiotty_warnings(kiotty_core_test)
kiotty_no_exceptions(kiotty_core_test)

add_executable(kiotty_datasource_test
    tests/datasource/datasource_test.cpp
    src/implementation/data/datasource/kiotty_connection.cpp
    src/implementation/data/datasource/kiotty_query.cpp
    src/implementation/data/datasource/kiotty_row.cpp
    src/implementation/data/datasource/kiotty_savepoint.cpp)
target_include_directories(kiotty_datasource_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(kiotty_datasource_test PRIVATE kiotty_sqlite3)
kiotty_warnings(kiotty_datasource_test)
kiotty_no_exceptions(kiotty_datasource_test)

add_executable(kiotty_context_test
    tests/context/context_test.cpp
    src/implementation/context/kiotty_cursor_pool.cpp)
target_include_directories(kiotty_context_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src)
kiotty_warnings(kiotty_context_test)
kiotty_no_exceptions(kiotty_context_test)

add_executable(kiotty_model_test
    tests/model/model_test.cpp
    src/implementation/domain/entity/kiotty_item_id.cpp
    src/implementation/domain/entity/kiotty_kind.cpp
    src/implementation/domain/usecases/kiotty_model_catalog.cpp)
target_include_directories(kiotty_model_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src)
kiotty_warnings(kiotty_model_test)
kiotty_no_exceptions(kiotty_model_test)

add_executable(kiotty_api_test tests/api/api_test.cpp)
target_link_libraries(kiotty_api_test PRIVATE kiotty_spec)
kiotty_warnings(kiotty_api_test)
kiotty_no_exceptions(kiotty_api_test)
