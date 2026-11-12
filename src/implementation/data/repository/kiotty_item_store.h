#pragma once

#include "kiotty_spec.h"
#include "implementation/domain/entity/kiotty_failure.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/repository/kiotty_item_history.h"
#include "implementation/data/repository/kiotty_item_reader.h"
#include "implementation/data/repository/kiotty_item_tables.h"
#include "implementation/data/repository/kiotty_item_tags.h"
#include "implementation/data/repository/kiotty_item_writer.h"
#include "implementation/domain/entity/kiotty_version_records.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

class ItemMaintenance
{
public:
    ItemMaintenance(ItemTables& tables, entity::Failure& failure)
        : _tables(tables), _failure(failure)
    {}

    void clear();
    KiottySpecStatus rebuild();
    void prune(uint32_t base);

private:
    Span<const uint32_t> collectLatest();
    KiottySpecStatus place(const entity::ItemVersion& version);
    void linkParents();
    KiottySpecStatus loadTags();

    ItemTables& _tables;
    entity::Failure& _failure;
};

class ItemStore
{
public:
    ItemStore(entity::Failure& failure, const ItemCapacity& capacity)
        : _failure(failure), _tables(capacity)
    {}

    ItemTables& tables() { return _tables; }
    ItemMaintenance maintenance() { return ItemMaintenance(_tables, _failure); }
    ItemHistory history() { return ItemHistory(_tables); }
    ItemReader reader() { return ItemReader(_tables); }
    ItemSlots slots() const { return ItemSlots(_tables); }
    ItemTags tags() const
    { return ItemTags(viewVector(_tables.tags).subspan(0, _tables.tag_count).value); }
    ItemWriter writer() { return ItemWriter(_tables, _failure); }

private:
    entity::Failure& _failure;
    ItemTables _tables;
};

}
