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
    entity::IdText id {};
    uint32_t before {};
    uint32_t after {};
    uint32_t kind {};
    uint32_t change {};
    uint32_t fields {};
};

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
    bool decode(uint32_t key);
    uint32_t changedFields(uint32_t key, const entity::ItemVersion& before,
                           const entity::ItemVersion& after) const;

    ItemStore& _items;
    DiffRange _range;
    uint32_t _kind_mask;
    uint32_t _field_mask;
    Span<const uint32_t> _keys {};
    size_t _position {0};
    ItemDiffRow _current {};
};

}
