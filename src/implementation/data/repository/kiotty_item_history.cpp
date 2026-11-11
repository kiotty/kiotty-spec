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

IdParts splitId(Span<const char> id)
{
    size_t dash = id.length();
    for (size_t index = id.length(); index > 0 && dash == id.length(); --index) {
        if (id[index - 1] == '-') { dash = index - 1; }
    }
    IdParts parts {};
    parts.prefix = Span<const char>(id.data(), dash);
    if (dash == id.length()) { return parts; }
    const auto number =
        parseDecimal(Span<const char>(id.data() + dash + 1, id.length() - dash - 1), MAX_ID_NUMBER);
    parts.number = number ? static_cast<uint32_t>(number.value) : 0;
    return parts;
}

}

Result<uint32_t, KiottySpecStatus> ItemHistory::keyOf(Span<const char> id) const
{
    for (size_t index = 0; index < _t.names.length(); ++index) {
        if (viewOf(_t.names.valueAt(index).view()) == viewOf(id)) {
            return Result<uint32_t, KiottySpecStatus>::success(_t.names.keyAt(index));
        }
    }
    return Result<uint32_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_NOT_FOUND);
}

Result<IdText, KiottySpecStatus> ItemHistory::idOf(uint32_t key) const
{ return _t.names.find(key); }

Span<const TagVersion> ItemHistory::tagsOf(uint32_t key) const
{
    const Span<const TagVersion> all = _t.tag_versions.all();
    const TagVersion* first =
        std::lower_bound(all.begin(), all.end(), key,
                         [](const TagVersion& tag, uint32_t value) { return tag.key < value; });
    const TagVersion* last =
        std::upper_bound(first, all.end(), key,
                         [](uint32_t value, const TagVersion& tag) { return value < tag.key; });
    return Span<const TagVersion>(first, static_cast<size_t>(last - first));
}

uint32_t ItemHistory::lastWriteAt(uint32_t key, uint32_t checkpoint) const
{
    const ItemVersion* version = _t.versions.latest(key, checkpoint);
    uint32_t last = version == nullptr ? 0 : version->checkpoint;
    for (const TagVersion& tag : tagsOf(key)) {
        if (tag.checkpoint <= checkpoint && tag.checkpoint > last) { last = tag.checkpoint; }
    }
    return last;
}

uint64_t ItemHistory::tagAt(uint32_t key, uint32_t tag, uint32_t checkpoint) const
{
    const TagVersion* version = _t.tag_versions.latest(entity::TagIdentity{key, tag}, checkpoint);
    if (version == nullptr || version->removed != 0) { return 0; }
    return version->value;
}

bool ItemHistory::tagsDiffer(uint32_t key, uint32_t from, uint32_t to) const
{
    for (const TagVersion& tag : tagsOf(key)) {
        if (tagAt(key, tag.tag, from) != tagAt(key, tag.tag, to)) { return true; }
    }
    return false;
}

Result<IdText, KiottySpecStatus> ItemHistory::nextId(Span<const char> prefix) const
{
    uint32_t largest = 0;
    for (size_t index = 0; index < _t.names.length(); ++index) {
        const IdParts parts = splitId(_t.names.valueAt(index).view());
        const bool same = viewOf(parts.prefix) == viewOf(prefix);
        if (same && parts.number > largest) { largest = parts.number; }
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
    size_t count = 0;
    for (const ItemVersion& version : _t.versions.all()) {
        if (count > 0 && _t.order[count - 1] == version.key) { continue; }
        _t.order[count] = version.key;
        ++count;
    }
    const ItemTables& tables = _t;
    std::sort(_t.order.begin(), _t.order.begin() + static_cast<std::ptrdiff_t>(count),
              [&tables](uint32_t left, uint32_t right) {
                  const uint32_t left_kind =
                      static_cast<uint32_t>(tables.versions.latest(left, UINT32_MAX)->kind);
                  const uint32_t right_kind =
                      static_cast<uint32_t>(tables.versions.latest(right, UINT32_MAX)->kind);
                  if (left_kind != right_kind) { return left_kind < right_kind; }
                  return tables.names.find(left).value < tables.names.find(right).value;
              });
    return Span<const uint32_t>(_t.order.data(), count);
}

void ItemHistory::prune(uint32_t base)
{
    _t.versions.prune(base);
    const ItemTables& tables = _t;
    _t.tag_versions.retain([&tables](const TagVersion& tag) {
        return tables.versions.latest(tag.key, UINT32_MAX) != nullptr;
    });
    _t.tag_versions.prune(base);
    for (size_t index = _t.names.length(); index > 0; --index) {
        const uint32_t key = _t.names.keyAt(index - 1);
        if (_t.versions.latest(key, UINT32_MAX) != nullptr) { continue; }
        _t.bodies.erase(key);
        _t.names.erase(key);
    }
}

}
