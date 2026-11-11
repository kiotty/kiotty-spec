#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/repository/kiotty_item_tables.h"
#include "implementation/domain/entity/kiotty_item_records.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

class ItemReader
{
public:
    explicit ItemReader(ItemTables& tables) : _t(tables) {}

    Result<entity::ItemHandle, KiottySpecStatus> find(Span<const char> id) const;
    Result<entity::ItemHandle, KiottySpecStatus> findKey(uint32_t key) const;
    Result<const entity::ItemRecord*, KiottySpecStatus> get(entity::ItemHandle handle) const;
    entity::ItemHandle handleInIdOrder(size_t position) const
    { return _t.items.handleAt(_t.id_index.valueAt(position)); }
    entity::ItemHandle handleAt(size_t index) const { return _t.items.handleAt(index); }
    size_t capacity() const { return _t.items.capacity(); }
    uint32_t key(entity::ItemHandle handle) const;
    size_t count() const { return _t.id_index.length(); }

    Result<size_t, KiottySpecStatus> readBody(entity::ItemHandle handle, char* buffer,
                                              size_t capacity) const;

private:
    ItemTables& _t;
};

}
