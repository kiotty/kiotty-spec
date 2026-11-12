#include "implementation/data/repository/kiotty_item_history.h"

#include "implementation/core/kiotty_decimal.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_number.h"
#include "implementation/core/kiotty_text_view.h"

#include <algorithm>

namespace kiotty::spec::implementation {

namespace {

using entity::IdText;
using entity::ItemVersion;
using entity::TagVersion;

constexpr size_t MIN_ID_DIGITS = 3;
constexpr uint32_t MAX_ID_NUMBER = 100000000u;

struct IdParts {
    Span<const char> prefix {};
    uint32_t number {0};
};

static_assert(sizeof(IdParts) == 24, "unexpected padding");

IdParts splitId(Span<const char> id);
size_t collectKeys(Span<uint32_t> order, Span<const ItemVersion> versions);
void sortByKindId(Span<uint32_t> keys, const ItemTables& tables);
uint32_t kindOf(const ItemTables& tables, uint32_t key);
bool isTagBeforeKey(const TagVersion& tag, uint32_t key);
bool isKeyBeforeTag(uint32_t key, const TagVersion& tag);

}

Result<uint32_t, KiottySpecStatus> ItemHistory::keyOf(Span<const char> id) const
{
    const size_t count = _tables.names.length();
    const std::string_view wanted = viewOf(id);
    for (size_t index = 0; index < count; ++index) {
        const std::string_view name = viewOf(_tables.names.valueAt(index).view());
        if (name == wanted) {
            return Result<uint32_t, KiottySpecStatus>::success(_tables.names.keyAt(index));
        }
    }
    return Result<uint32_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
}

Result<IdText, KiottySpecStatus> ItemHistory::idOf(uint32_t key) const
{ return _tables.names.find(key); }

uint32_t ItemHistory::lastWriteAt(uint32_t key, uint32_t checkpoint) const
{
    const auto version = _tables.versions.latest(key, checkpoint);
    uint32_t last = version ? (*version.value).checkpoint : 0;
    for (const TagVersion& tag : tagsOf(key)) {
        if (tag.checkpoint <= checkpoint && tag.checkpoint > last) { last = tag.checkpoint; }
    }
    return last;
}

bool ItemHistory::tagsDiffer(uint32_t key, uint32_t from, uint32_t to) const
{
    for (const TagVersion& tag : tagsOf(key)) {
        const uint64_t from_value = tagAt(key, tag.tag, from);
        const uint64_t to_value = tagAt(key, tag.tag, to);
        if (from_value != to_value) { return true; }
    }
    return false;
}

Span<const TagVersion> ItemHistory::tagsOf(uint32_t key) const
{
    const Span<const TagVersion> all = _tables.tag_versions.all();
    const size_t first = static_cast<size_t>(
        std::lower_bound(all.begin(), all.end(), key, isTagBeforeKey) - all.begin());
    const size_t last = static_cast<size_t>(
        std::upper_bound(all.begin() + first, all.end(), key, isKeyBeforeTag) - all.begin());
    const Result<Span<const TagVersion>, KiottySpecStatus> tags = all.subspan(first, last - first);
    return tags.value;
}

uint64_t ItemHistory::tagAt(uint32_t key, uint32_t tag, uint32_t checkpoint) const
{
    const auto version = _tables.tag_versions.latest(entity::TagIdentity{key, tag}, checkpoint);
    const bool is_live = version && (*version.value).removed == 0;
    if (!is_live) { return 0; }
    return (*version.value).value;
}

Result<IdText, KiottySpecStatus> ItemHistory::nextId(Span<const char> prefix) const
{
    uint32_t largest = 0;
    const size_t count = _tables.names.length();
    for (size_t index = 0; index < count; ++index) {
        const IdParts parts = splitId(_tables.names.valueAt(index).view());
        const bool is_same_prefix = viewOf(parts.prefix) == viewOf(prefix);
        if (is_same_prefix && parts.number > largest) { largest = parts.number; }
    }
    IdText id {};
    KiottySpecStatus status = id.assign(prefix);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = id.append(literalText("-")); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) {
        const DecimalText number = formatDecimal(largest + 1, MIN_ID_DIGITS);
        status = id.append(number.view());
    }
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        return Result<IdText, KiottySpecStatus>::failure(status);
    }
    return Result<IdText, KiottySpecStatus>::success(id);
}

Span<const uint32_t> ItemHistory::keysByIdentity()
{
    const Span<uint32_t> order = viewVector(_tables.order);
    const size_t count = collectKeys(order, _tables.versions.all());
    const Span<uint32_t> keys(order.begin(), count);
    sortByKindId(keys, _tables);
    return keys;
}

namespace {

IdParts splitId(Span<const char> id)
{
    const size_t length = id.length();
    size_t dash = length;
    for (size_t index = length; index > 0 && dash == length; --index) {
        const Result<const char&, KiottySpecStatus> letter = id.at(index - 1);
        const bool is_dash = letter && *letter.value == '-';
        if (is_dash) { dash = index - 1; }
    }
    IdParts parts {};
    const auto prefix = id.subspan(0, dash);
    parts.prefix = prefix.value;
    if (dash == length) { return parts; }
    const auto digits = id.subspan(dash + 1, length - dash - 1);
    const Result<uint64_t, KiottySpecStatus> number = parseDecimal(digits.value, MAX_ID_NUMBER);
    parts.number = number ? static_cast<uint32_t>(number.value) : 0;
    return parts;
}

size_t collectKeys(Span<uint32_t> order, Span<const ItemVersion> versions)
{
    size_t count = 0;
    uint32_t previous = 0;
    for (const ItemVersion& version : versions) {
        const bool is_repeated = count > 0 && previous == version.key;
        if (is_repeated) { continue; }
        const Result<uint32_t&, KiottySpecStatus> slot = order.at(count);
        if (!slot) { return count; }
        *slot.value = version.key;
        previous = version.key;
        ++count;
    }
    return count;
}

void sortByKindId(Span<uint32_t> keys, const ItemTables& tables)
{
    std::sort(keys.begin(), keys.end(), [&tables](uint32_t left, uint32_t right) {
        const uint32_t left_kind = kindOf(tables, left);
        const uint32_t right_kind = kindOf(tables, right);
        if (left_kind != right_kind) { return left_kind < right_kind; }
        return tables.names.find(left).value < tables.names.find(right).value;
    });
}

uint32_t kindOf(const ItemTables& tables, uint32_t key)
{
    const auto version = tables.versions.latest(key, UINT32_MAX);
    return version ? static_cast<uint32_t>((*version.value).kind) : 0;
}

bool isTagBeforeKey(const TagVersion& tag, uint32_t key) { return tag.item.index < key; }

bool isKeyBeforeTag(uint32_t key, const TagVersion& tag) { return key < tag.item.index; }

}

}
