#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"

#include <cstddef>

namespace kiotty::spec::implementation {

// 파일을 줄 단위로 읽는다. 줄은 남이 준 buffer 에 담기며, 한 줄이 buffer 보다 길면 CAPACITY_EXCEEDED.
// 줄 끝의 '\r' 은 뺀다. 공백은 내용이므로 남긴다. 빈 줄도 돌려준다.
class FileReader
{
public:
    explicit FileReader(Span<char> buffer) : _buffer(buffer) {}
    ~FileReader();

    FileReader(const FileReader&) = delete;
    FileReader& operator=(const FileReader&) = delete;

    // 없거나 열 수 없으면 NOT_FOUND
    KiottySpecStatus open(Span<const char> path);
    // 다음 줄이 있으면 true 이고 line() 이 그 줄이다. 다음 호출이 line() 을 덮어쓴다.
    Result<bool, KiottySpecStatus> next();
    Span<const char> line() const { return _line; }

private:
    Span<char> _buffer;
    Span<const char> _line {};
    int _descriptor {-1};
    size_t _start {0};
    size_t _end {0};
    bool _finished {false};
};

}
