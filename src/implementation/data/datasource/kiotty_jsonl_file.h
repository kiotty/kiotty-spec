#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_json_reader.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_file_reader.h"
#include "implementation/data/datasource/kiotty_file_system.h"

#include <string_view>

namespace kiotty::spec::implementation {

// directory/table.jsonl
SystemPathResult jsonlPath(Span<const char> directory, std::string_view table);

// jsonl 파일 하나를 줄마다 객체로 읽는다. 빈 줄은 건너뛰고, 줄 전체가 객체인지 먼저 확인한다.
// 줄은 line 에, 확인할 때 푼 값은 scratch 에 둔다. 두 영역은 이 객체보다 오래 살아야 한다.
class JsonlFile
{
public:
    JsonlFile(Span<char> line, Span<char> scratch) : _file(line), _scratch(scratch) {}

    // directory/table.jsonl 을 연다. 파일이 없으면 줄이 없는 것으로 본다.
    KiottySpecStatus open(Span<const char> directory, std::string_view table);
    // 다음 객체가 있으면 true. 객체가 아니면 INVALID_ARGUMENT
    Result<bool, KiottySpecStatus> next();
    JsonReader& row() { return _row; }

private:
    FileReader _file;
    Span<char> _scratch;
    JsonReader _row {Span<const char>()};
    bool _present {false};
};

}
