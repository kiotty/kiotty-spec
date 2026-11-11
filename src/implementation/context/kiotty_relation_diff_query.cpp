#include "implementation/context/kiotty_relation_diff_query.h"

namespace kiotty::spec::implementation {

namespace {

using entity::RelationVersion;

constexpr uint32_t MAX_KIND_BIT = 32;

bool isKindInMask(uint32_t mask, uint32_t kind);
bool isLive(const RelationVersion* version);

}

KiottySpecStatus RelationDiffQuery::open()
{
    _firsts = _relations.relationsInDiffOrder();
    _position = 0;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<bool, KiottySpecStatus> RelationDiffQuery::next()
{
    const size_t first_count = _firsts.length();
    while (_position < first_count) {
        const uint32_t first = _firsts[_position];
        ++_position;
        const bool is_decoded = decode(first);
        if (is_decoded) { return Result<bool, KiottySpecStatus>::success(true); }
    }
    return Result<bool, KiottySpecStatus>::success(false);
}

bool RelationDiffQuery::decode(uint32_t first)
{
    const RelationVersion& any = _relations.versions().all()[first];
    const bool is_in_mask = isKindInMask(_kind_mask, static_cast<uint32_t>(any.model));
    if (!is_in_mask) { return false; }
    const RelationVersion* before = _relations.versions().latest(any.identity(), _range.from);
    const RelationVersion* after = _relations.versions().latest(any.identity(), _range.to);
    const bool is_live_before = isLive(before);
    const bool is_live_after = isLive(after);
    if (is_live_before == is_live_after) { return false; }
    const ItemTables& tables = _items.tables();
    _current = RelationDiffRow{};
    _current.from_id = tables.names.find(any.from).value;
    _current.to_id = tables.names.find(any.to).value;
    _current.relation = any.relation;
    _current.model = static_cast<uint32_t>(any.model);
    _current.checkpoint = after != nullptr ? after->checkpoint : 0;
    _current.change = is_live_after ? KIOTTY_SPEC_CHANGE_ADDED : KIOTTY_SPEC_CHANGE_REMOVED;
    return true;
}

namespace {

bool isKindInMask(uint32_t mask, uint32_t kind)
{ return mask == 0 || (kind >= 1 && kind <= MAX_KIND_BIT && (mask & (1u << (kind - 1))) != 0); }

bool isLive(const RelationVersion* version) { return version != nullptr && version->removed == 0; }

}

}
