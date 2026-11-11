#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_item_records.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

class ItemTags
{
public:
    explicit ItemTags(Span<const entity::TagRecord> tags) : _tags(tags) {}

    Span<const entity::TagRecord> all() const { return _tags; }
    Span<const entity::TagRecord> of(entity::ItemHandle handle) const;
    uint64_t value(entity::ItemHandle handle, uint32_t tag) const;
    size_t position(uint32_t index, uint32_t tag) const;

private:
    Span<const entity::TagRecord> _tags;
};

}
