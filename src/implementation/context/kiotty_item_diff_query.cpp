#include "implementation/context/kiotty_item_diff_query.h"

namespace kiotty::spec::implementation {

namespace {

using entity::ItemVersion;

constexpr uint32_t MAX_KIND_BIT = 32;

bool inMask(uint32_t mask, uint32_t kind)
{ return mask == 0 || (kind >= 1 && kind <= MAX_KIND_BIT && (mask & (1u << (kind - 1))) != 0); }

bool isLive(const ItemVersion* version) { return version != nullptr && version->removed == 0; }

}

KiottySpecStatus ItemDiffQuery::open()
{
    _keys = _items.history().keysByKindAndId();
    _position = 0;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<bool, KiottySpecStatus> ItemDiffQuery::next()
{
    while (_position < _keys.length()) {
        const uint32_t key = _keys[_position];
        ++_position;
        if (decode(key)) { return Result<bool, KiottySpecStatus>::success(true); }
    }
    return Result<bool, KiottySpecStatus>::success(false);
}

bool ItemDiffQuery::decode(uint32_t key)
{
    const ItemTables& tables = _items.tables();
    const ItemVersion* before = tables.versions.latest(key, _range.from);
    const ItemVersion* after = tables.versions.latest(key, _range.to);
    const ItemVersion* known = after != nullptr ? after : before;
    if (known == nullptr || !inMask(_kind_mask, static_cast<uint32_t>(known->kind))) {
        return false;
    }
    _current = ItemDiffRow{};
    _current.id = tables.names.find(key).value;
    _current.kind = static_cast<uint32_t>(known->kind);
    _current.before = isLive(before) ? _items.history().lastWriteAt(key, _range.from) : 0;
    _current.after = after != nullptr ? _items.history().lastWriteAt(key, _range.to) : 0;
    if (!isLive(before) && isLive(after)) { _current.change = KIOTTY_SPEC_CHANGE_ADDED; }
    if (isLive(before) && !isLive(after)) { _current.change = KIOTTY_SPEC_CHANGE_REMOVED; }
    if (isLive(before) && isLive(after)) { _current.fields = changedFields(key, *before, *after); }
    const bool wanted =
        _current.fields != 0 && (_field_mask == 0 || (_current.fields & _field_mask) != 0);
    if (wanted) { _current.change = KIOTTY_SPEC_CHANGE_MODIFIED; }
    return _current.change != 0;
}

uint32_t ItemDiffQuery::changedFields(uint32_t key, const ItemVersion& before,
                                      const ItemVersion& after) const
{
    uint32_t fields = 0;
    if (!(before.title == after.title)) { fields |= KIOTTY_SPEC_CHANGED_FIELD_TITLE; }
    if (!(before.summary == after.summary)) { fields |= KIOTTY_SPEC_CHANGED_FIELD_SUMMARY; }
    if (before.parent != after.parent) { fields |= KIOTTY_SPEC_CHANGED_FIELD_PARENT; }
    if (_items.history().tagsDiffer(key, _range.from, _range.to)) {
        fields |= KIOTTY_SPEC_CHANGED_FIELD_TAGS;
    }
    if (before.body_hash != after.body_hash) { fields |= KIOTTY_SPEC_CHANGED_FIELD_BODY; }
    return fields;
}

}
