#include "implementation/data/repository/kiotty_item_tags.h"

namespace kiotty::spec::implementation {

using entity::ItemHandle;
using entity::TagRecord;

size_t ItemTags::position(uint32_t index, uint32_t tag) const
{
    size_t low = 0;
    size_t high = _tags.length();
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        const TagRecord& probe = _tags[middle];
        if (probe.item.index < index || (probe.item.index == index && probe.tag < tag)) { low = middle + 1; } else { high = middle; }
    }
    return low;
}

Span<const TagRecord> ItemTags::of(ItemHandle handle) const
{
    const size_t first = position(handle.index, 0);
    size_t last = first;
    while (last < _tags.length() && _tags[last].item.index == handle.index) { ++last; }
    return Span<const TagRecord>(_tags.data() + first, last - first);
}

uint64_t ItemTags::value(ItemHandle handle, uint32_t tag) const
{
    const size_t found = position(handle.index, tag);
    if (found < _tags.length() && _tags[found].item.index == handle.index && _tags[found].tag == tag) { return _tags[found].value; }
    return 0;
}

}
