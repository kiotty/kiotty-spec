#pragma once

#include "kiotty_spec.h"
#include "implementation/context/kiotty_diff_range.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/data/datasource/kiotty_storage.h"
#include "implementation/domain/entity/kiotty_item_records.h"

#include <cstdint>

namespace kiotty::spec::implementation {

struct RelationDiffRow {
    entity::IdText from_id;
    entity::IdText to_id;
    uint32_t checkpoint;
    uint32_t relation;
    uint32_t model;
    uint32_t change;
};

// 두 체크포인트 사이에 생기거나 없어진 관계 (설계서 v1 5.4). 모델 Kind 순이다.
// open 한 뒤 next 가 true 인 동안 current() 를 읽는다.
class RelationDiffQuery
{
public:
    RelationDiffQuery(Storage& storage, DiffRange range, uint32_t kind_mask)
        : _storage(storage), _range(range), _kind_mask(kind_mask)
    {
    }

    KiottySpecStatus open();
    Result<bool, KiottySpecStatus> next();
    const RelationDiffRow& current() const { return _current; }

private:
    // 이 행이 diff 이면 _current 에 담고 true
    bool decode(const Row& row);

    Storage& _storage;
    DiffRange _range;
    uint32_t _kind_mask;
    Statement _statement;
    RelationDiffRow _current {};
};

}
