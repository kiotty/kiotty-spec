# 브라우저 뷰어 다리 (DSN-116, DSN-117, DSN-127)
# - kiotty_viewer_bridge: 모든 빌드에서 만드는 정적 라이브러리. 라이브러리 C interface 만 부른다.
# - kiotty_viewer: emscripten 빌드에서만. viewer.js + viewer.wasm (ES 모듈) 을 만든다.

add_library(kiotty_viewer_bridge STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/kiotty_viewer_bridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/kiotty_viewer_query.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/kiotty_viewer_items.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/kiotty_viewer_item.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/kiotty_viewer_model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/kiotty_viewer_names.cpp)
target_include_directories(kiotty_viewer_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kiotty_viewer_bridge PUBLIC kiotty_spec)
kiotty_warnings(kiotty_viewer_bridge)
kiotty_no_exceptions(kiotty_viewer_bridge)
kiotty_windows_definitions(kiotty_viewer_bridge)

if(EMSCRIPTEN)
    # 내보낼 함수는 모두 kiotty_viewer_bridge 안에 있다. 실행 파일은 링크만 한다.
    set(KIOTTY_VIEWER_ENTRY ${CMAKE_CURRENT_BINARY_DIR}/kiotty_viewer_entry.cpp)
    file(WRITE ${KIOTTY_VIEWER_ENTRY} "")
    add_executable(kiotty_viewer ${KIOTTY_VIEWER_ENTRY})
    target_link_libraries(kiotty_viewer PRIVATE -Wl,--whole-archive kiotty_viewer_bridge -Wl,--no-whole-archive)
    kiotty_warnings(kiotty_viewer)
    kiotty_no_exceptions(kiotty_viewer)
    set_target_properties(kiotty_viewer PROPERTIES OUTPUT_NAME viewer SUFFIX .js)
    target_link_options(kiotty_viewer PRIVATE
        --no-entry
        -sMODULARIZE=1
        -sEXPORT_ES6=1
        -sEXPORT_NAME=createKiottyViewer
        -sENVIRONMENT=web,node
        -sALLOW_MEMORY_GROWTH=1
        -sFILESYSTEM=1
        -sFORCE_FILESYSTEM=1
        "-sEXPORTED_FUNCTIONS=_KiottyViewer_open,_KiottyViewer_query,_KiottyViewer_close,_malloc,_free"
        "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,FS,UTF8ToString,stringToUTF8,lengthBytesUTF8")
endif()
