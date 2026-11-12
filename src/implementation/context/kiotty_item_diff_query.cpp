#include "implementation/context/kiotty_item_diff_query.h"

namespace kiotty::spec::implementation {

namespace {

using entity::ItemVersion;
using VersionResult = Result<const ItemVersion&, KiottySpecStatus>;

constexpr uint32_t MAX_KIND_BIT = 32;

bool isKindInMask(uint32_t mask, uint32_t kind);
bool isLive(const VersionResult& version);
bool isFieldWanted(uint32_t fields, uint32_t field_mask);

}

KiottySpecStatus ItemDiffQuery::open()
{
    _keys = _items.history().keysByIdentity();
    _position = 0;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<bool, KiottySpecStatus> ItemDiffQuery::next()
{
    const Span<const uint32_t> keys = _keys;
    const size_t key_count = keys.length();
    size_t position = _position;
    bool is_decoded = false;
    while (position < key_count) {
        const Result<const uint32_t&, KiottySpecStatus> key = keys.at(position);
        if (!key) { return Result<bool, KiottySpecStatus>::failure(key.code); }
        ++position;
        is_decoded = decode(*key.value);
        if (is_decoded) { break; }
    }
    _position = position;
    return Result<bool, KiottySpecStatus>::success(is_decoded);
}

bool ItemDiffQuery::decode(uint32_t key)
{
    const ItemTables& tables = _items.tables();
    const VersionResult before = tables.versions.latest(key, _range.from);
    const VersionResult after = tables.versions.latest(key, _range.to);
    const VersionResult known = after ? after : before;
    if (!known) { return false; }
    const uint32_t kind = static_cast<uint32_t>((*known.value).kind);
    const bool is_in_mask = isKindInMask(_kind_mask, kind);
    if (!is_in_mask) { return false; }
    _current = ItemDiffRow{};
    _current.id = tables.names.find(key).value;
    _current.kind = kind;
    const bool is_live_before = isLive(before);
    const bool is_live_after = isLive(after);
    _current.before = is_live_before ? _items.history().lastWriteAt(key, _range.from) : 0;
    _current.after = after ? _items.history().lastWriteAt(key, _range.to) : 0;
    const bool has_liveness_change = is_live_before != is_live_after;
    if (has_liveness_change) {
        _current.change = is_live_after ? KIOTTY_SPEC_CHANGE_ADDED : KIOTTY_SPEC_CHANGE_REMOVED;
    }
    if (is_live_before && is_live_after) {
        _current.fields = changedFields(key, *before.value, *after.value);
    }
    const bool is_wanted = isFieldWanted(_current.fields, _field_mask);
    if (is_wanted) { _current.change = KIOTTY_SPEC_CHANGE_MODIFIED; }
    return _current.change != 0;
}

uint32_t ItemDiffQuery::changedFields(uint32_t key, const ItemVersion& before,
                                      const ItemVersion& after) const
{
    uint32_t fields = 0;
    const ItemLabelPool& labels = _items.tables().version_labels;
    const entity::ItemVersionLabel& before_label = labels.read(before.label);
    const entity::ItemVersionLabel& after_label = labels.read(after.label);
    if (before_label.title != after_label.title) { fields |= KIOTTY_SPEC_CHANGED_FIELD_TITLE; }
    if (before_label.summary != after_label.summary) {
        fields |= KIOTTY_SPEC_CHANGED_FIELD_SUMMARY;
    }
    if (before.parent.index != after.parent.index) { fields |= KIOTTY_SPEC_CHANGED_FIELD_PARENT; }
    const bool has_tag_change = _items.history().tagsDiffer(key, _range.from, _range.to);
    if (has_tag_change) { fields |= KIOTTY_SPEC_CHANGED_FIELD_TAGS; }
    if (before.body_hash != after.body_hash) { fields |= KIOTTY_SPEC_CHANGED_FIELD_BODY; }
    return fields;
}

namespace {

bool isKindInMask(uint32_t mask, uint32_t kind)
{ return mask == 0 || (kind >= 1 && kind <= MAX_KIND_BIT && (mask & (1u << (kind - 1))) != 0); }

bool isLive(const VersionResult& version) { return version && (*version.value).removed == 0; }

bool isFieldWanted(uint32_t fields, uint32_t field_mask)
{ return fields != 0 && (field_mask == 0 || (fields & field_mask) != 0); }

}

}
