#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_kisp_file.h"
#include "implementation/data/datasource/kiotty_kisp_line.h"
#include "implementation/domain/usecases/kiotty_spec_tables.h"

namespace kiotty::spec::implementation {

// store 의 버전 표를 정렬된 줄로 쓴다. 태그 · 관계 · Kind 는 이름으로 쓴다.
// SpecFile 의 세부 클래스 (개발수칙 4.19). area 는 한 줄을 쌓는 영역이다.
class SpecWriter
{
public:
    SpecWriter(const SpecTables& tables, KispFileWriter& file, Span<char> area) : _t(tables), _file(file), _area(area) {}

    KiottySpecStatus write();

private:
    KiottySpecStatus writeMeta();
    KiottySpecStatus writeCheckpoints();
    KiottySpecStatus writeItems();
    KiottySpecStatus writeItemVersions();
    KiottySpecStatus writeTags();
    KiottySpecStatus writeRelations();
    KiottySpecStatus writeDecisions();
    KiottySpecStatus writeHeader(std::string_view columns);
    KiottySpecStatus emit(KispLineEncoder& line);
    KispLineEncoder startLine(std::string_view table, uint32_t removed);

    SpecTables _t;
    KispFileWriter& _file;
    Span<char> _area;
};

}
