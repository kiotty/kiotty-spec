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

IdParts splitId(Span<const char> id);
size_t collectKeys(Span<uint32_t> order, Span<const ItemVersion> versions);
void sortByKindId(Span<uint32_t> keys, const ItemTables& tables);

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
    const ItemVersion* version = _tables.versions.latest(key, checkpoint);
    uint32_t last = version == nullptr ? 0 : version->checkpoint;
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
    const TagVersion* first =
        std::lower_bound(all.begin(), all.end(), key,
                         [](const TagVersion& tag, uint32_t value) { return tag.key < value; });
    const TagVersion* last =
        std::upper_bound(first, all.end(), key,
                         [](uint32_t value, const TagVersion& tag) { return value < tag.key; });
    return Span<const TagVersion>(first, static_cast<size_t>(last - first));
}

uint64_t ItemHistory::tagAt(uint32_t key, uint32_t tag, uint32_t checkpoint) const
{
    const TagVersion* version =
        _tables.tag_versions.latest(entity::TagIdentity{key, tag}, checkpoint);
    if (version == nullptr || version->removed != 0) { return 0; }
    return version->value;
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
        status = appendDecimal(id, largest + 1, MIN_ID_DIGITS);
    }
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        return Result<IdText, KiottySpecStatus>::failure(status);
    }
    return Result<IdText, KiottySpecStatus>::success(id);
}

Span<const uint32_t> ItemHistory::keysByKindAndId()
{
    const Span<uint32_t> order = viewVector(_tables.order);
    const size_t count = collectKeys(order, _tables.versions.all());
    const Span<uint32_t> keys(order.begin(), count);
    sortByKindId(keys, _tables);
    return keys;
}

void ItemHistory::prune(uint32_t base)
{
    _tables.versions.prune(base);
    const ItemTables& tables = _tables;
    _tables.tag_versions.retain([&tables](const TagVersion& tag) {
        return tables.versions.latest(tag.key, UINT32_MAX) != nullptr;
    });
    _tables.tag_versions.prune(base);
    for (size_t index = _tables.names.length(); index > 0; --index) {
        const uint32_t key = _tables.names.keyAt(index - 1);
        const bool has_version = _tables.versions.latest(key, UINT32_MAX) != nullptr;
        if (has_version) { continue; }
        _tables.bodies.erase(key);
        _tables.names.erase(key);
    }
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
    parts.prefix = Span<const char>(id.data(), dash);
    if (dash == length) { return parts; }
    const auto number =
        parseDecimal(Span<const char>(id.data() + dash + 1, length - dash - 1), MAX_ID_NUMBER);
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
        const uint32_t left_kind =
            static_cast<uint32_t>(tables.versions.latest(left, UINT32_MAX)->kind);
        const uint32_t right_kind =
            static_cast<uint32_t>(tables.versions.latest(right, UINT32_MAX)->kind);
        if (left_kind != right_kind) { return left_kind < right_kind; }
        return tables.names.find(left).value < tables.names.find(right).value;
    });
}

}

}
