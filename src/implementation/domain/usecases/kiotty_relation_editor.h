#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/usecases/kiotty_lifecycle.h"
#include "implementation/domain/usecases/kiotty_workspace.h"

#include <cstdint>

namespace kiotty::spec::implementation {

// 관계 쓰기. 모델의 관계 정의로 허용 쌍을 검사하고, 체크포인트에 묶어 DB 와 풀을 함께 고친다.
class RelationEditor
{
public:
    RelationEditor(Workspace& workspace, Lifecycle& lifecycle) : _w(workspace), _lifecycle(lifecycle) {}

    KiottySpecStatus add(Span<const char> from_id, uint32_t relation, Span<const char> to_id);
    KiottySpecStatus remove(Span<const char> from_id, uint32_t relation, Span<const char> to_id);

private:
    Result<entity::RelationRecord, KiottySpecStatus> relationOf(Span<const char> from_id, uint32_t relation, Span<const char> to_id);

    Workspace& _w;
    Lifecycle& _lifecycle;
};

}
