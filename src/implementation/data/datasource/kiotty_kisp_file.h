#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_file_reader.h"
#include "implementation/data/datasource/kiotty_file_system.h"
#include "implementation/data/datasource/kiotty_file_writer.h"
#include "implementation/data/datasource/kiotty_kisp_line.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

// 빈 파일 (또는 없는 파일) 의 줄 해시
constexpr uint64_t EMPTY_KISP_HASH = 0xcbf29ce484222325ull;

// .kisp 를 줄마다 읽는다. 읽은 줄의 해시를 쌓아, 쓰기 전에 파일이 밖에서 바뀌었는지 비교하는 데 쓴다.
class KispFileReader
{
public:
    KispFileReader(Span<char> buffer, Span<char> decode_area) : _file(buffer), _decoder(decode_area) {}

    // 없으면 NOT_FOUND. 그러면 줄이 없는 파일로 본다.
    KiottySpecStatus open(Span<const char> path);
    // 다음 줄이 있으면 true 이고 line() 이 그 줄이다.
    Result<bool, KiottySpecStatus> next();
    Span<const char> line() const { return _file.line(); }
    Result<KispFields, KiottySpecStatus> fields() const { return _decoder.decode(_file.line()); }
    size_t lineNumber() const { return _line_number; }
    uint64_t hash() const { return _hash; }

private:
    FileReader _file;
    KispLineDecoder _decoder;
    size_t _line_number {0};
    uint64_t _hash {EMPTY_KISP_HASH};
    bool _present {false};
};

// <path>.tmp 에 쓰고 commit 에서 path 로 바꾼다. commit 하지 않고 사라지면 임시 파일을 지운다.
class KispFileWriter
{
public:
    KispFileWriter() = default;
    ~KispFileWriter();

    KispFileWriter(const KispFileWriter&) = delete;
    KispFileWriter& operator=(const KispFileWriter&) = delete;

    KiottySpecStatus open(Span<const char> path);
    // 줄바꿈까지 든 한 줄
    KiottySpecStatus write(Span<const char> line);
    KiottySpecStatus commit();
    uint64_t hash() const { return _hash; }

private:
    FileWriter _file;
    SystemPath _path;
    SystemPath _temporary;
    uint64_t _hash {EMPTY_KISP_HASH};
    bool _open {false};
};

// 파일의 줄 해시. 없으면 EMPTY_KISP_HASH
Result<uint64_t, KiottySpecStatus> hashKispFile(Span<const char> path, Span<char> buffer);

}
