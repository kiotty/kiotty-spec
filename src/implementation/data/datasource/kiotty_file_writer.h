#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_span.h"

namespace kiotty::spec::implementation {

// 파일을 새로 만들어 조각마다 이어 쓴다. 소멸자가 닫는다.
class FileWriter
{
public:
    FileWriter() = default;
    ~FileWriter();

    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;

    // 있으면 비운다. 열 수 없으면 INPUT_OUTPUT
    KiottySpecStatus open(Span<const char> path);
    KiottySpecStatus write(Span<const char> content);
    void close();

private:
    int _descriptor {-1};
};

}
