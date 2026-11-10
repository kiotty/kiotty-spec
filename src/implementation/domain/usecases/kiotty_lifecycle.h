#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/kiotty_checker.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

// 체크포인트 흐름 (설계서 v1 5.1, 5.3). 쓰기는 가장 최근 체크포인트에 묶인다.
class Lifecycle
{
public:
    Lifecycle(Workspace& workspace, Checker& checker) : _w(workspace), _checker(checker) {}

    // 쓰기를 묶을 체크포인트. 없으면 PRECONDITION, DONE 이면 CONFIRMED. STARTED 면 RUNNING 으로 바꾼다. WriteScope 가 부른다.
    Result<uint32_t, KiottySpecStatus> beginWrite();

    Result<uint32_t, KiottySpecStatus> start(entity::Step step, Span<const char> description);
    KiottySpecStatus resume();
    KiottySpecStatus complete(KiottySpecActor actor);
    Result<size_t, KiottySpecStatus> prune(KiottySpecActor actor, uint32_t base);

private:
    KiottySpecStatus requireUser(KiottySpecActor actor);

    Workspace& _w;
    Checker& _checker;
};

}
