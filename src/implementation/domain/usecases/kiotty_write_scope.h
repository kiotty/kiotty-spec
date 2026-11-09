#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/data/datasource/kiotty_savepoint.h"
#include "implementation/domain/usecases/kiotty_lifecycle.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstdint>

namespace kiotty::spec::implementation {

// 쓰기 하나를 저장점과 가장 최근 체크포인트에 묶는다. 만들 때 저장점을 열고 체크포인트를 고른다.
// 부른 자리에서 쓰고 버린다. finish 를 부르지 않고 사라지면 저장점은 되돌려진다.
class WriteScope
{
public:
    WriteScope(Workspace& workspace, Lifecycle& lifecycle);

    WriteScope(const WriteScope&) = delete;
    WriteScope& operator=(const WriteScope&) = delete;

    // 쓰기를 묶을 체크포인트 sequence. 저장점이나 체크포인트를 얻지 못했으면 그 이유
    Result<uint32_t, KiottySpecStatus> sequence() const { return _sequence; }
    // status 가 성공이면 커밋한다. 실패하면 DB 를 되돌리고 풀을 다시 적재한다. 최종 상태를 돌려준다.
    KiottySpecStatus finish(KiottySpecStatus status);

private:
    Workspace& _w;
    Savepoint _savepoint;
    Result<uint32_t, KiottySpecStatus> _sequence;
    bool _touched {false};
};

}
