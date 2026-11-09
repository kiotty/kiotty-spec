#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_json_reader.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/core/kiotty_text_buffer.h"
#include "implementation/data/datasource/kiotty_schema.h"
#include "implementation/domain/usecases/kiotty_import_areas.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstddef>

namespace kiotty::spec::implementation {

// jsonl 교환 (설계서 v1 9.3 교환). 테이블 하나가 파일 하나이고, 줄은 키 순서다.
// 힙을 쓰지 않는다. 한 줄씩 읽고 쓰며, 줄과 값은 작업 버퍼에 둔다. 옛 형식은 LegacyImporter 가 옮긴다.
class Exchange
{
public:
    Exchange(Workspace& workspace, TextBuffer& work) : _w(workspace), _work(work) {}

    KiottySpecStatus exportJsonl(Span<const char> directory);
    // jsonl 로 DB 를 다시 만든다. 옛 형식이면 변환하고, 잘라 내거나 건너뛴 것을 report 에 한 줄씩 적는다.
    // report 에 다 들어가지 않는 줄은 빠진다. 값은 보고한 줄 수
    Result<size_t, KiottySpecStatus> importJsonl(Span<const char> directory, Span<char> report);

private:
    ImportAreas areas();
    KiottySpecStatus exportTable(Span<const char> directory, const OrderedTable& table);
    Result<size_t, KiottySpecStatus> importAll(Span<const char> directory, Span<char> report);
    KiottySpecStatus importCurrent(Span<const char> directory, const ImportAreas& areas);
    KiottySpecStatus importTable(Span<const char> directory, const OrderedTable& table, const ImportAreas& areas);
    KiottySpecStatus insertRow(const OrderedTable& table, JsonReader& row, Span<char> scratch);

    Workspace& _w;
    TextBuffer& _work;
};

}
