#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_json_reader.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/core/kiotty_span_text.h"
#include "implementation/domain/usecases/kiotty_import_areas.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace kiotty::spec::implementation {

// 정의서 v0 의 jsonl 을 설계서 v1 스키마로 옮긴다 (설계서 v1 11절). 빈 스키마에 넣는다.
// 잘라 내거나 건너뛴 것을 report 에 한 줄씩 적는다.
class LegacyImporter
{
public:
    LegacyImporter(Workspace& workspace, const ImportAreas& areas, SpanText& report) : _w(workspace), _areas(areas), _report(report) {}

    // 값은 보고한 줄 수
    Result<size_t, KiottySpecStatus> run(Span<const char> directory);

private:
    using Insert = KiottySpecStatus (LegacyImporter::*)(JsonReader&);

    // table 의 줄마다 insert 를 부른다. 파일이 없으면 부르지 않는다.
    KiottySpecStatus eachRow(Span<const char> directory, std::string_view table, Insert insert);
    KiottySpecStatus importMeta(Span<const char> directory);
    KiottySpecStatus insertMeta(JsonReader& row);
    KiottySpecStatus importIdCounters(Span<const char> directory);
    KiottySpecStatus insertIdCounter(JsonReader& row);
    KiottySpecStatus importCheckpoints(Span<const char> directory);
    KiottySpecStatus insertCheckpoint(JsonReader& row);
    KiottySpecStatus linkCheckpoints();
    KiottySpecStatus importItems(Span<const char> directory);
    KiottySpecStatus insertItem(JsonReader& row);
    KiottySpecStatus importLinks(Span<const char> directory);
    KiottySpecStatus insertLink(JsonReader& row);
    KiottySpecStatus importDecisions(Span<const char> directory);
    KiottySpecStatus insertDecision(JsonReader& row);
    KiottySpecStatus countSkipped(Span<const char> directory, std::string_view table);
    KiottySpecStatus countRow(JsonReader& row);
    std::string_view kindOf(Span<const char> id);
    int64_t checkpointOf(JsonReader& row);
    void note(std::initializer_list<Span<const char>> parts);

    Workspace& _w;
    const ImportAreas& _areas;
    SpanText& _report;
    size_t _notes {0};
    size_t _pending {0};
    size_t _file_links {0};
    size_t _skipped_rows {0};
    int64_t _latest {0};
};

}
