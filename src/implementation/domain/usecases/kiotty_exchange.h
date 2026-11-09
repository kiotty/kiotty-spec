#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/core/kiotty_span_text.h"
#include "implementation/core/kiotty_text_buffer.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstddef>
#include <string_view>

namespace kiotty::spec::implementation {

class JsonReader;

// jsonl 교환 (설계서 v1 9.3 교환). 테이블 하나가 파일 하나이고, 줄은 키 순서다.
// 힙을 쓰지 않는다. 한 줄씩 읽고 쓰며, 줄과 값은 작업 버퍼에 둔다.
class Exchange
{
public:
    Exchange(Workspace& workspace, TextBuffer& work) : _w(workspace), _work(work) {}

    KiottySpecStatus exportJsonl(Span<const char> directory);
    // jsonl 로 DB 를 다시 만든다. 옛 형식이면 변환하고, 잘라 내거나 건너뛴 것을 report 에 한 줄씩 적는다.
    // report 에 다 들어가지 않는 줄은 빠진다. 값은 보고한 줄 수
    Result<size_t, KiottySpecStatus> importJsonl(Span<const char> directory, Span<char> report);

private:
    // 작업 버퍼를 나눈 영역. 줄 · 짧은 값 둘 · 긴 값
    struct Areas {
        Span<char> line;
        Span<char> first;
        Span<char> second;
        Span<char> text;
    };

    Areas areas();
    KiottySpecStatus importCurrent(Span<const char> directory, const Areas& areas);
    KiottySpecStatus importLegacy(Span<const char> directory, const Areas& areas, SpanText& report, size_t& notes);
    // table 파일의 줄마다 visit(JsonReader&) 를 부른다. 파일이 없으면 부르지 않는다.
    template <typename Visit>
    KiottySpecStatus eachRow(Span<const char> directory, std::string_view table, const Areas& areas, Visit visit);

    Workspace& _w;
    TextBuffer& _work;
};

}
