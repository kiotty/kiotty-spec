#pragma once

#include "implementation/context/cursor_pool.h"
#include "implementation/context/project.h"
#include "implementation/core/text_buffer.h"

#include <cstddef>
#include <string_view>

namespace kiotty::spec::implementation {

constexpr size_t DEFAULT_MAX_CURSORS = 16;
constexpr size_t DEFAULT_MAX_CURSOR_ROWS = 1024;
constexpr size_t DEFAULT_WORK_BUFFER_BYTES = 256 * 1024;

struct Configuration {
    size_t maxCursors {DEFAULT_MAX_CURSORS};
    size_t maxCursorRows {DEFAULT_MAX_CURSOR_ROWS};
    size_t workBufferBytes {DEFAULT_WORK_BUFFER_BYTES};
};

// 라이브러리 인스턴스 하나가 쓰는 모든 것을 소유한다 (개발수칙 1.2).
// 커서 풀과 작업 버퍼는 생성자에서 확보하고, 소멸자가 전부 정리한다.
class Context {
public:
    explicit Context(const Configuration &configuration)
        : _cursors(configuration.maxCursors, configuration.maxCursorRows), _work(configuration.workBufferBytes) {}

    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;

    KiottySpecStatus open(std::string_view projectRoot) { return _project.open(projectRoot); }
    Project &project() { return _project; }
    CursorPool &cursors() { return _cursors; }
    TextBuffer &work() { return _work; }

private:
    Project _project;
    CursorPool _cursors;
    TextBuffer _work;
};

}
