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
    explicit ItemReader(ItemTables& tables) : _tables(tables) {}

    Result<entity::ItemHandle, KiottySpecStatus> find(Span<const char> id) const;
    Result<entity::ItemHandle, KiottySpecStatus> findKey(uint32_t key) const;
    Result<const entity::ItemState&, KiottySpecStatus> get(entity::ItemHandle handle) const;
    Result<const entity::ItemLabel&, KiottySpecStatus> label(entity::ItemHandle handle) const;
    uint32_t key(entity::ItemHandle handle) const;

    Result<size_t, KiottySpecStatus> readBody(entity::ItemHandle handle, Span<char> buffer) const;

private:
    ItemTables& _tables;
};

class ItemSlots
{
public:
    explicit ItemSlots(const ItemTables& tables) : _tables(tables) {}

    entity::ItemHandle handleAt(size_t index) const { return _tables.items.handleAt(index); }
    size_t capacity() const { return _tables.items.capacity(); }
    size_t count() const { return _tables.id_index.length(); }
    uint32_t readIdOrder(size_t position) const { return _tables.id_index.valueAt(position); }

private:
    const ItemTables& _tables;
};

}
