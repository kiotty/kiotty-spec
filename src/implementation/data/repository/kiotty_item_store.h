#pragma once

#include "kiotty_spec.h"
#include "implementation/domain/entity/kiotty_failure.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/repository/kiotty_item_history.h"
#include "implementation/data/repository/kiotty_item_reader.h"
#include "implementation/data/repository/kiotty_item_tables.h"
#include "implementation/data/repository/kiotty_item_tags.h"
#include "implementation/data/repository/kiotty_item_writer.h"

#include <cstddef>

namespace kiotty::spec::implementation {

class ItemStore
{
public:
    ItemStore(entity::Failure& failure, const ItemCapacity& capacity)
        : _failure(failure), _tables(capacity)
    {}

    void clear();
    KiottySpecStatus rebuild();

    ItemTables& tables() { return _tables; }
    ItemHistory history() { return ItemHistory(_tables); }
    ItemReader reader() { return ItemReader(_tables); }
    ItemTags tags() const
    { return ItemTags(Span<const entity::TagRecord>(_tables.tags.data(), _tables.tag_count)); }
    ItemWriter writer() { return ItemWriter(_tables, _failure); }

private:
    Span<const uint32_t> collectLatest();
    KiottySpecStatus place(const entity::ItemVersion& version);
    void linkParents();
    KiottySpecStatus loadTags();

    entity::Failure& _failure;
    ItemTables _tables;
};

}
