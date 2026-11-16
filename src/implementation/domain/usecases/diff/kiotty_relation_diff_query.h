#pragma once

#include "kiotty_spec.h"
#include "implementation/domain/entity/kiotty_diff_records.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/repository/item/kiotty_item_store.h"
#include "implementation/data/repository/relation/kiotty_relation_store.h"
#include "implementation/domain/entity/kiotty_item_records.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

class RelationDiffQuery
{
public:
    RelationDiffQuery(ItemStore& items, RelationStore& relations, entity::DiffRange range,
                      uint32_t kind_mask)
        : _items(items), _relations(relations), _range(range), _kind_mask(kind_mask),
          _firsts(relations.relationsInDiffOrder())
    {
    }

    Result<bool, KiottySpecStatus> next();
    const entity::RelationDiffRow& current() const { return _current; }

private:
    Result<bool, KiottySpecStatus> decode(uint32_t first);

    ItemStore& _items;
    RelationStore& _relations;
    entity::DiffRange _range;
    uint32_t _kind_mask;
    const Span<const uint32_t> _firsts;
    size_t _position {0};
    entity::RelationDiffRow _current {};
};

}
