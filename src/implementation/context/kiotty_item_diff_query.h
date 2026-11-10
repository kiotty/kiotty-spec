#pragma once

#include "kiotty_spec.h"
#include "implementation/context/kiotty_diff_range.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/repository/kiotty_item_store.h"
#include "implementation/domain/entity/kiotty_item_records.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

struct ItemDiffRow {
    entity::IdText id;
    uint32_t before;
    uint32_t after;
    uint32_t kind;
    uint32_t change;
    uint32_t fields;
};

// 두 체크포인트 사이에 추가 · 삭제 · 수정된 항목 (설계서 v1 5.4). Kind 순, ID 순이다.
// open 한 뒤 next 가 true 인 동안 current() 를 읽는다.
class ItemDiffQuery
{
public:
    ItemDiffQuery(ItemStore& items, DiffRange range, uint32_t kind_mask, uint32_t field_mask)
        : _items(items), _range(range), _kind_mask(kind_mask), _field_mask(field_mask)
    {
    }

    KiottySpecStatus open();
    Result<bool, KiottySpecStatus> next();
    const ItemDiffRow& current() const { return _current; }

private:
    // 이 key 가 diff 이면 _current 에 담고 true
    bool decode(uint32_t key);
    uint32_t changedFields(uint32_t key, const entity::ItemVersion& before, const entity::ItemVersion& after) const;

    ItemStore& _items;
    DiffRange _range;
    uint32_t _kind_mask;
    uint32_t _field_mask;
    Span<const uint32_t> _keys;
    size_t _position {0};
    ItemDiffRow _current {};
};

}
