#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_savepoint.h"
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

    // 저장점 안에서 가장 최근 체크포인트에 묶어 operation(sequence) 를 부른다.
    // 실패하면 DB 를 되돌리고 풀을 다시 적재한다.
    template <typename Operation>
    KiottySpecStatus write(Operation operation)
    {
        KiottySpecStatus status = KIOTTY_SPEC_STATUS_SUCCESS;
        bool touched = false;
        {
            Savepoint savepoint(_w.storage.connection());
            status = _w.storage.check(savepoint.begin());
            if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
                const auto sequence = beginWrite();
                touched = true;
                status = sequence ? operation(sequence.value) : sequence.code;
            }
            if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = _w.storage.check(savepoint.commit()); }
        }
        if (status != KIOTTY_SPEC_STATUS_SUCCESS && touched) { _w.reload(); }
        return status;
    }

    Result<uint32_t, KiottySpecStatus> start(entity::Step step, Span<const char> description);
    KiottySpecStatus resume();
    KiottySpecStatus complete(KiottySpecActor actor);
    Result<size_t, KiottySpecStatus> prune(KiottySpecActor actor, uint32_t base);

private:
    // 쓰기를 묶을 체크포인트. 없으면 PRECONDITION, DONE 이면 CONFIRMED. STARTED 면 RUNNING 으로 바꾼다.
    Result<uint32_t, KiottySpecStatus> beginWrite();
    KiottySpecStatus requireUser(KiottySpecActor actor, Span<const char> what);

    Workspace& _w;
    Checker& _checker;
};

}
