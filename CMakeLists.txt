cmake_minimum_required(VERSION 3.24)

project(kiotty_spec VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC 기본 플래그의 /EHsc 를 빼고 타깃마다 kiotty_no_exceptions 로 정한다.
if(MSVC)
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

# ---------------------------------------------------------------------------
# 공통 컴파일 옵션
# ---------------------------------------------------------------------------
function(kiotty_warnings target)
    if(MSVC)
        # enum switch 에서 빠진 멤버는 에러 (개발수칙 2.8)
        target_compile_options(${target} PRIVATE /W4 /utf-8 /permissive- /Zc:__cplusplus /we4062)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror=switch)
    endif()
endfunction()

# 예외 없이 빌드한다 (개발수칙 5.1).
function(kiotty_no_exceptions target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /EHs-c-)
        target_compile_definitions(${target} PRIVATE _HAS_EXCEPTIONS=0)
    else()
        target_compile_options(${target} PRIVATE -fno-exceptions)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# 라이브러리: include/kiotty_spec.h 의 C 인터페이스 구현
# ---------------------------------------------------------------------------
file(GLOB_RECURSE KIOTTY_IMPL_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/implementation/*.cpp)

file(GLOB KIOTTY_ADAPTER_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(kiotty_spec STATIC
    ${KIOTTY_ADAPTER_SOURCES}
    ${KIOTTY_IMPL_SOURCES})
target_include_directories(kiotty_spec
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
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
    src/implementation/data/datasource/kiotty_kisp_file.cpp
    src/implementation/data/datasource/kiotty_kisp_line.cpp
    src/implementation/data/datasource/kiotty_file_system.cpp
    src/implementation/data/datasource/kiotty_system_path.cpp
    src/implementation/data/datasource/backend/kiotty_platform_windows.cpp
    src/implementation/data/datasource/backend/kiotty_platform_posix.cpp
    src/implementation/data/datasource/kiotty_file_reader.cpp
    src/implementation/data/datasource/kiotty_file_writer.cpp)
target_include_directories(kiotty_datasource_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src)
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
    src/implementation/domain/usecases/kiotty_model_catalog.cpp
    src/implementation/domain/usecases/kiotty_model_finder.cpp)
target_include_directories(kiotty_model_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src)
kiotty_warnings(kiotty_model_test)
kiotty_no_exceptions(kiotty_model_test)

add_executable(kiotty_api_test tests/api/api_test.cpp)
target_link_libraries(kiotty_api_test PRIVATE kiotty_spec)
kiotty_warnings(kiotty_api_test)
kiotty_no_exceptions(kiotty_api_test)

# 시스템 테스트: 기능 요구사항의 인수 조건을 C 인터페이스로 실행한다 (ST-001 ~ ST-031).
add_executable(kiotty_system_test tests/system/system_test.cpp)
target_link_libraries(kiotty_system_test PRIVATE kiotty_spec)
kiotty_warnings(kiotty_system_test)
kiotty_no_exceptions(kiotty_system_test)
