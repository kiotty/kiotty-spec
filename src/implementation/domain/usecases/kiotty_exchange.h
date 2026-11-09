#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstddef>
#include <string>

namespace kiotty::spec::implementation {

// jsonl 교환 (설계서 v1 9.3 교환). 테이블 하나가 파일 하나이고, 줄은 키 순서다.
class Exchange
{
public:
    explicit Exchange(Workspace& workspace) : _w(workspace) {}

    KiottySpecStatus exportJsonl(Span<const char> directory);
    // DB 내용을 jsonl 로 바꾼다. 옛 형식이면 변환하고, 잘라 내거나 건너뛴 것을 report 에 한 줄씩 적는다.
    // 값은 report 의 줄 수
    Result<size_t, KiottySpecStatus> importJsonl(Span<const char> directory, std::string& report);

private:
    KiottySpecStatus importCurrent(Span<const char> directory);
    KiottySpecStatus importLegacy(Span<const char> directory, std::string& report, size_t& notes);

    Workspace& _w;
};

}
