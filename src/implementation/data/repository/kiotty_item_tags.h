#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/domain/entity/kiotty_item_records.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

// ItemStore 의 태그 풀을 읽는 쪽 (개발수칙 4.19). 레코드는 (item index, tag) 순으로 정렬되어 있다.
class ItemTags
{
public:
    explicit ItemTags(Span<const entity::TagRecord> tags) : _tags(tags) {}

    Span<const entity::TagRecord> all() const { return _tags; }
    Span<const entity::TagRecord> of(entity::ItemHandle handle) const;
    // 없으면 0
    uint64_t value(entity::ItemHandle handle, uint32_t tag) const;
    // (index, tag) 보다 작지 않은 첫 위치
    size_t position(uint32_t index, uint32_t tag) const;

private:
    Span<const entity::TagRecord> _tags;
};

}
