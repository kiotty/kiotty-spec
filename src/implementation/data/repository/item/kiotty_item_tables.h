#pragma once

#include "implementation/core/kiotty_detail_pool.h"
#include "implementation/core/kiotty_fixed_map.h"
#include "implementation/core/kiotty_lazy_pool.h"
#include "implementation/core/kiotty_version_table.h"
#include "implementation/data/repository/item/kiotty_body_store.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/entity/kiotty_version_records.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

using ItemLabelPool = DetailPool<entity::ItemVersionLabel, entity::ItemLabelHandle>;

struct ItemCapacity {
    size_t items {};
    size_t tags {};
    size_t versions {};
    size_t tag_versions {};
    size_t body_bytes {};
};

static_assert(sizeof(ItemCapacity) == 40, "unexpected padding");

struct ItemTables {
    explicit ItemTables(const ItemCapacity& capacity)
        : names(capacity.versions),
          versions(capacity.versions),
          tag_versions(capacity.tag_versions),
          version_labels(capacity.versions + 1),
          items(capacity.items),
          labels(capacity.items),
          id_index(capacity.items),
          key_index(capacity.items),
          keys(capacity.items, entity::NO_KEY),
          parents(capacity.items, entity::NO_KEY),
          tags(capacity.tags),
          order(capacity.versions),
          bodies(capacity.body_bytes, capacity.versions)
    {
    }

    FixedMap<uint32_t, entity::IdText> names;
    VersionTable<entity::ItemVersion> versions;
    VersionTable<entity::TagVersion> tag_versions;
    ItemLabelPool version_labels;

    LazyPool<entity::ItemState, entity::ItemHandle> items;
    std::vector<entity::ItemLabel> labels;
    FixedMap<entity::IdText, uint32_t> id_index;
    FixedMap<uint32_t, uint32_t> key_index;
    std::vector<uint32_t> keys;
    std::vector<uint32_t> parents;
    std::vector<entity::TagRecord> tags;
    size_t tag_count {0};

    std::vector<uint32_t> order;
    BodyStore bodies;
};

inline Handle pickLabelHandle(const entity::ItemVersion& version) { return version.label; }

}
