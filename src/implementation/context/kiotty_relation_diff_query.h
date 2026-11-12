#pragma once

#include "kiotty_spec.h"
#include "implementation/context/kiotty_diff_range.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/repository/kiotty_item_store.h"
#include "implementation/data/repository/kiotty_relation_store.h"
#include "implementation/domain/entity/kiotty_item_records.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

struct RelationDiffRow {
    entity::IdText from_id {};
    entity::IdText to_id {};
    uint32_t checkpoint {};
    uint32_t relation {};
    uint32_t model {};
    uint32_t change {};
};

static_assert(sizeof(RelationDiffRow) == 48, "unexpected padding");

class RelationDiffQuery
{
public:
    RelationDiffQuery(ItemStore& items, RelationStore& relations, DiffRange range,
                      uint32_t kind_mask)
        : _items(items), _relations(relations), _range(range), _kind_mask(kind_mask)
    {
    }

    KiottySpecStatus open();
    Result<bool, KiottySpecStatus> next();
    const RelationDiffRow& current() const { return _current; }

private:
    Result<bool, KiottySpecStatus> decode(uint32_t first);

    ItemStore& _items;
    RelationStore& _relations;
    DiffRange _range;
    uint32_t _kind_mask;
    Span<const uint32_t> _firsts {};
    size_t _position {0};
    RelationDiffRow _current {};
};

}
