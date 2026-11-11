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
    ItemStore(entity::Failure& failure, const ItemCapacity& capacity) : _failure(failure), _t(capacity) {}

    void clear();
    KiottySpecStatus rebuild();

    ItemTables& tables() { return _t; }
    ItemHistory history() { return ItemHistory(_t); }
    ItemReader reader() { return ItemReader(_t); }
    ItemTags tags() const { return ItemTags(Span<const entity::TagRecord>(_t.tags.data(), _t.tag_count)); }
    ItemWriter writer() { return ItemWriter(_t, _failure); }

private:
    size_t collectLatest();
    KiottySpecStatus place(const entity::ItemVersion& version);
    void linkParents();
    KiottySpecStatus loadTags();

    entity::Failure& _failure;
    ItemTables _t;
};

}
