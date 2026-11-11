#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/repository/kiotty_item_tables.h"
#include "implementation/domain/entity/kiotty_version_records.h"

#include <cstdint>

namespace kiotty::spec::implementation {

class ItemHistory
{
public:
    explicit ItemHistory(ItemTables& tables) : _tables(tables) {}

    Result<uint32_t, KiottySpecStatus> keyOf(Span<const char> id) const;
    Result<entity::IdText, KiottySpecStatus> idOf(uint32_t key) const;
    uint32_t lastWriteAt(uint32_t key, uint32_t checkpoint) const;
    uint64_t tagAt(uint32_t key, uint32_t tag, uint32_t checkpoint) const;
    bool tagsDiffer(uint32_t key, uint32_t from, uint32_t to) const;
    Result<entity::IdText, KiottySpecStatus> nextId(Span<const char> prefix) const;
    Span<const uint32_t> keysByKindAndId();
    void prune(uint32_t base);

private:
    Span<const entity::TagVersion> tagsOf(uint32_t key) const;

    ItemTables& _tables;
};

}
