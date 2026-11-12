#include "implementation/context/kiotty_relation_diff_query.h"

namespace kiotty::spec::implementation {

namespace {

using entity::RelationVersion;
using VersionResult = Result<const RelationVersion&, KiottySpecStatus>;

constexpr uint32_t MAX_KIND_BIT = 32;

bool isKindInMask(uint32_t mask, uint32_t kind);
bool isLive(const VersionResult& version);

}

KiottySpecStatus RelationDiffQuery::open()
{
    _firsts = _relations.relationsInDiffOrder();
    _position = 0;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Result<bool, KiottySpecStatus> RelationDiffQuery::next()
{
    const Span<const uint32_t> firsts = _firsts;
    const size_t first_count = firsts.length();
    size_t position = _position;
    bool is_decoded = false;
    while (position < first_count) {
        const Result<const uint32_t&, KiottySpecStatus> first = firsts.at(position);
        if (!first) { return Result<bool, KiottySpecStatus>::failure(first.code); }
        ++position;
        const Result<bool, KiottySpecStatus> decoded = decode(*first.value);
        if (!decoded) { return decoded; }
        is_decoded = decoded.value;
        if (is_decoded) { break; }
    }
    _position = position;
    return Result<bool, KiottySpecStatus>::success(is_decoded);
}

Result<bool, KiottySpecStatus> RelationDiffQuery::decode(uint32_t first)
{
    using DecodeResult = Result<bool, KiottySpecStatus>;
    const Span<const RelationVersion> versions = _relations.versions().all();
    const Result<const RelationVersion&, KiottySpecStatus> found = versions.at(first);
    if (!found) { return DecodeResult::failure(found.code); }
    const RelationVersion& any = *found.value;
    const bool is_in_mask = isKindInMask(_kind_mask, static_cast<uint32_t>(any.model));
    if (!is_in_mask) { return DecodeResult::success(false); }
    const VersionResult before = _relations.versions().latest(any.identity(), _range.from);
    const VersionResult after = _relations.versions().latest(any.identity(), _range.to);
    const bool is_live_before = isLive(before);
    const bool is_live_after = isLive(after);
    if (is_live_before == is_live_after) { return DecodeResult::success(false); }
    const ItemTables& tables = _items.tables();
    _current = RelationDiffRow{};
    _current.from_id = tables.names.find(any.from).value;
    _current.to_id = tables.names.find(any.to).value;
    _current.relation = any.relation;
    _current.model = static_cast<uint32_t>(any.model);
    _current.checkpoint = after ? (*after.value).checkpoint : 0;
    _current.change = is_live_after ? KIOTTY_SPEC_CHANGE_ADDED : KIOTTY_SPEC_CHANGE_REMOVED;
    return DecodeResult::success(true);
}

namespace {

bool isKindInMask(uint32_t mask, uint32_t kind)
{ return mask == 0 || (kind >= 1 && kind <= MAX_KIND_BIT && (mask & (1u << (kind - 1))) != 0); }

bool isLive(const VersionResult& version) { return version && (*version.value).removed == 0; }

}

}
