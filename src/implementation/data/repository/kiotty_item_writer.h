#pragma once

#include "kiotty_spec.h"
#include "implementation/domain/entity/kiotty_failure.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/repository/kiotty_item_reader.h"
#include "implementation/data/repository/kiotty_item_tables.h"
#include "implementation/data/repository/kiotty_item_tags.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/entity/kiotty_version_records.h"

#include <cstdint>

namespace kiotty::spec::implementation {

class ItemWriter
{
public:
    ItemWriter(ItemTables& tables, entity::Failure& failure) : _tables(tables), _failure(failure) {}

    Result<entity::ItemHandle, KiottySpecStatus> create(const entity::ItemRecord& record,
                                                        uint32_t sequence);
    KiottySpecStatus update(entity::ItemHandle handle, const entity::ItemRecord& record,
                            uint32_t sequence);
    KiottySpecStatus setTag(entity::ItemHandle handle, uint32_t tag, uint64_t value,
                            uint32_t sequence);
    KiottySpecStatus writeBody(entity::ItemHandle handle, Span<const char> body, uint32_t sequence);
    KiottySpecStatus remove(entity::ItemHandle handle, uint32_t sequence);
    KiottySpecStatus rename(entity::ItemHandle handle, entity::IdText id);

private:
    ItemReader reader() const { return ItemReader(_tables); }
    ItemTags tags() const
    { return ItemTags(viewVector(_tables.tags).subspan(0, _tables.tag_count).value); }
    KiottySpecStatus checkUnused(entity::IdText id);
    Result<uint32_t, KiottySpecStatus> newKey(entity::IdText id) const;
    KiottySpecStatus writeVersion(entity::ItemHandle handle, const entity::ItemVersion& version);
    Result<entity::ItemVersion, KiottySpecStatus> currentVersion(entity::ItemHandle handle) const;
    KiottySpecStatus applyTag(entity::ItemHandle handle, entity::ItemKind kind, uint32_t tag,
                              uint64_t value);
    void dropTags(entity::ItemHandle handle);
    Span<uint32_t> viewKeys() const
    { return viewVector(_tables.keys); }
    Span<uint32_t> viewParents() const
    { return viewVector(_tables.parents); }
    Span<entity::TagRecord> viewTags() const
    { return viewVector(_tables.tags); }

    ItemTables& _tables;
    entity::Failure& _failure;
};

}
