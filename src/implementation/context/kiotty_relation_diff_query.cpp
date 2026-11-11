#include "implementation/context/kiotty_relation_diff_query.h"

namespace kiotty::spec::implementation {

namespace {

using entity::RelationVersion;

constexpr uint32_t MAX_KIND_BIT = 32;

bool inMask(uint32_t mask, uint32_t kind)
{ return mask == 0 || (kind >= 1 && kind <= MAX_KIND_BIT && (mask & (1u << (kind - 1))) != 0); }

bool isLive(const RelationVersion* version) { return version != nullptr && version->removed == 0; }

}

KiottySpecStatus RelationDiffQuery::open()
{
    _firsts = _relations.relationsInDiffOrder();
    _position = 0;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<bool, KiottySpecStatus> RelationDiffQuery::next()
{
    while (_position < _firsts.length()) {
        const uint32_t first = _firsts[_position];
        ++_position;
        if (decode(first)) { return Result<bool, KiottySpecStatus>::success(true); }
    }
    return Result<bool, KiottySpecStatus>::success(false);
}

bool RelationDiffQuery::decode(uint32_t first)
{
    const RelationVersion& any = _relations.versions().all()[first];
    if (!inMask(_kind_mask, static_cast<uint32_t>(any.model))) { return false; }
    const RelationVersion* before = _relations.versions().latest(any.identity(), _range.from);
    const RelationVersion* after = _relations.versions().latest(any.identity(), _range.to);
    if (isLive(before) == isLive(after)) { return false; }
    const ItemTables& tables = _items.tables();
    _current = RelationDiffRow{};
    _current.from_id = tables.names.find(any.from).value;
    _current.to_id = tables.names.find(any.to).value;
    _current.relation = any.relation;
    _current.model = static_cast<uint32_t>(any.model);
    _current.checkpoint = after != nullptr ? after->checkpoint : 0;
    _current.change = isLive(after) ? KIOTTY_SPEC_CHANGE_ADDED : KIOTTY_SPEC_CHANGE_REMOVED;
    return true;
}

}
