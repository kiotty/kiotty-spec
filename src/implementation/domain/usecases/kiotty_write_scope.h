#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/domain/usecases/kiotty_lifecycle.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstdint>

namespace kiotty::spec::implementation {

// 쓰기 하나를 가장 최근 체크포인트에 묶는다. 만들 때 체크포인트를 고른다.
// 부른 자리에서 쓰고 버린다. finish 가 성공이면 파일에 옮기고, 실패면 파일에서 다시 읽는다.
class WriteScope
{
public:
    WriteScope(Workspace& workspace, Lifecycle& lifecycle) : _w(workspace), _sequence(lifecycle.beginWrite()) {}

    WriteScope(const WriteScope&) = delete;
    WriteScope& operator=(const WriteScope&) = delete;

    // 쓰기를 묶을 체크포인트 sequence. 얻지 못했으면 그 이유
    Result<uint32_t, KiottySpecStatus> sequence() const { return _sequence; }
    // 최종 상태를 돌려준다.
    KiottySpecStatus finish(KiottySpecStatus status) { return _w.finish(_sequence ? status : _sequence.code); }

private:
    Workspace& _w;
    Result<uint32_t, KiottySpecStatus> _sequence;
};

}
