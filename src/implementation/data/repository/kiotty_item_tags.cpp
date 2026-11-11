#include "implementation/data/repository/kiotty_item_tags.h"

namespace kiotty::spec::implementation {

using entity::ItemHandle;
using entity::TagRecord;

namespace {

constexpr size_t BISECTION_DIVISOR = 2;

}

Span<const TagRecord> ItemTags::of(ItemHandle handle) const
{
    const size_t first = position(handle.index, 0);
    size_t last = first;
    const size_t length = _tags.length();
    while (last < length && _tags[last].item.index == handle.index) { ++last; }
    return Span<const TagRecord>(_tags.data() + first, last - first);
}

uint64_t ItemTags::value(ItemHandle handle, uint32_t tag) const
{
    const size_t found = position(handle.index, tag);
    const size_t length = _tags.length();
    if (found < length && _tags[found].item.index == handle.index &&
        _tags[found].tag == tag) {
        return _tags[found].value;
    }
    return 0;
}

size_t ItemTags::position(uint32_t index, uint32_t tag) const
{
    size_t low = 0;
    size_t high = _tags.length();
    while (low < high) {
        const size_t middle = low + (high - low) / BISECTION_DIVISOR;
        const TagRecord& probe = _tags[middle];
        if (probe.item.index < index || (probe.item.index == index && probe.tag < tag)) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

}
