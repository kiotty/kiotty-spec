#include "implementation/data/repository/kiotty_item_tags.h"

namespace kiotty::spec::implementation {

using entity::ItemHandle;
using entity::TagRecord;

namespace {

constexpr size_t BISECTION_DIVISOR = 2;

Span<const TagRecord> findItemTags(Span<const TagRecord> tags, uint32_t index);
uint64_t findTagValue(Span<const TagRecord> tags, uint32_t index, uint32_t tag);
size_t findTagPosition(Span<const TagRecord> tags, uint32_t index, uint32_t tag);
bool isRecordBefore(const TagRecord& record, uint32_t index, uint32_t tag);

}

Span<const TagRecord> ItemTags::of(ItemHandle handle) const
{ return findItemTags(_tags, handle.index); }

uint64_t ItemTags::value(ItemHandle handle, uint32_t tag) const
{ return findTagValue(_tags, handle.index, tag); }

size_t ItemTags::position(uint32_t index, uint32_t tag) const
{ return findTagPosition(_tags, index, tag); }

namespace {

Span<const TagRecord> findItemTags(Span<const TagRecord> tags, uint32_t index)
{
    const size_t first = findTagPosition(tags, index, 0);
    size_t last = first;
    const size_t length = tags.length();
    for (size_t position = first; position < length; ++position) {
        const bool is_other_item = tags[position].item.index != index;
        if (is_other_item) { break; }
        last = position + 1;
    }
    return tags.subspan(first, last - first).value;
}

uint64_t findTagValue(Span<const TagRecord> tags, uint32_t index, uint32_t tag)
{
    const size_t found = findTagPosition(tags, index, tag);
    const Result<const TagRecord&, KiottySpecStatus> record = tags.at(found);
    if (!record) { return 0; }
    const TagRecord& candidate = *record.value;
    const bool is_match = candidate.item.index == index && candidate.tag == tag;
    return is_match ? candidate.value : 0;
}

size_t findTagPosition(Span<const TagRecord> tags, uint32_t index, uint32_t tag)
{
    size_t low = 0;
    size_t high = tags.length();
    while (low < high) {
        const size_t middle = low + (high - low) / BISECTION_DIVISOR;
        const Result<const TagRecord&, KiottySpecStatus> probe = tags.at(middle);
        const bool is_before = probe && isRecordBefore(*probe.value, index, tag);
        if (is_before) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

bool isRecordBefore(const TagRecord& record, uint32_t index, uint32_t tag)
{ return record.item.index < index || (record.item.index == index && record.tag < tag); }

}

}
