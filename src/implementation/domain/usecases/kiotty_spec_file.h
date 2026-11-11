#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/core/kiotty_text_buffer.h"
#include "implementation/data/datasource/kiotty_file_system.h"
#include "implementation/data/datasource/kiotty_kisp_file.h"
#include "implementation/domain/usecases/kiotty_spec_tables.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

// .kisp 파일과 store 를 오간다. 열 때 전부 읽어 버전 표와 현재 뷰를 만들고, 쓰기마다 전부 다시 쓴다.
// 다시 쓰기 전에 파일이 밖에서 (git pull, 다른 ks) 바뀌었으면 CONFLICT 로 거부한다.
class SpecFile
{
public:
    SpecFile(const SpecTables& tables, TextBuffer& work) : _t(tables), _work(work) {}

    // 파일이 없으면 빈 프로젝트다.
    KiottySpecStatus open(Span<const char> database);
    KiottySpecStatus load();
    KiottySpecStatus save();
    // 연 뒤에 파일이 밖에서 바뀌었으면 CONFLICT
    KiottySpecStatus checkUnchanged();

private:
    void clear();
    KiottySpecStatus readLines(KispFileReader& reader);
    KiottySpecStatus readLine(KispFileReader& reader);
    KiottySpecStatus finishLoad();
    KiottySpecStatus lineFailure(size_t line_number, KiottySpecStatus status);

    SpecTables _t;
    TextBuffer& _work;
    SystemPath _database {};
    uint64_t _hash {EMPTY_KISP_HASH};
};

}
