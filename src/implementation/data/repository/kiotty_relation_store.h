#pragma once

#include "kiotty_spec.h"
#include "implementation/domain/entity/kiotty_failure.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/core/kiotty_version_table.h"
#include "implementation/data/repository/kiotty_item_store.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/entity/kiotty_version_records.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

struct RelationDependencies {
    entity::Failure& failure;
    ItemStore& items;
};

static_assert(sizeof(RelationDependencies) == 16, "unexpected padding");

class RelationWriter
{
public:
    RelationWriter(RelationDependencies dependencies,
                   VersionTable<entity::RelationVersion>& versions,
                   std::vector<entity::RelationRecord>& records, size_t& length)
        : _failure(dependencies.failure), _items(dependencies.items), _versions(versions),
          _records(records), _length(length)
    {}

    KiottySpecStatus add(const entity::RelationRecord& relation, uint32_t sequence);
    KiottySpecStatus remove(const entity::RelationRecord& relation, uint32_t sequence);
    KiottySpecStatus removeAllOf(entity::ItemHandle item, uint32_t sequence);

private:
    Span<const entity::RelationRecord> viewUsed() const
    { return viewVector(_records).subspan(0, _length).value; }
    Span<entity::RelationRecord> viewRecords() const
    { return viewVector(_records); }
    KiottySpecStatus writeVersion(const entity::RelationRecord& relation, uint32_t removed,
                                  uint32_t sequence);

    entity::Failure& _failure;
    ItemStore& _items;
    VersionTable<entity::RelationVersion>& _versions;
    std::vector<entity::RelationRecord>& _records;
    size_t& _length;
};

class RelationStore
{
public:
    RelationStore(entity::Failure& failure, ItemStore& items, size_t capacity, size_t versions)
        : _failure(failure), _items(items), _versions(versions), _records(capacity),
          _order(versions)
    {
    }

    VersionTable<entity::RelationVersion>& versions() { return _versions; }
    Span<const uint32_t> relationsInDiffOrder();
    KiottySpecStatus rebuild();

    Span<const entity::RelationRecord> all() const
    { return viewVector(_records).subspan(0, _length).value; }
    bool contains(const entity::RelationRecord& relation) const;

    RelationWriter writer()
    {
        return RelationWriter(RelationDependencies {_failure, _items}, _versions, _records,
                              _length);
    }

private:
    Span<entity::RelationRecord> viewRecords()
    { return viewVector(_records); }
    size_t lowerBound(const entity::RelationRecord& relation) const;

    entity::Failure& _failure;
    ItemStore& _items;
    VersionTable<entity::RelationVersion> _versions;
    std::vector<entity::RelationRecord> _records;
    size_t _length {0};
    std::vector<uint32_t> _order;
};

}
