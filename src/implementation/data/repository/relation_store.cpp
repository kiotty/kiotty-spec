#include "implementation/data/repository/relation_store.h"

#include "implementation/core/text_view.h"

#include <algorithm>

namespace kiotty::spec::implementation {

namespace {

using entity::ItemHandle;
using entity::RelationRecord;

constexpr char LOAD_SQL[] =
    "SELECT v.from_id, v.model, v.relation, v.to_id FROM relation_version v "
    "JOIN (SELECT from_id, relation, to_id, MAX(checkpoint) AS latest FROM relation_version GROUP BY from_id, relation, to_id) m "
    "ON v.from_id = m.from_id AND v.relation = m.relation AND v.to_id = m.to_id AND v.checkpoint = m.latest WHERE v.removed = 0";

constexpr char WRITE_SQL[] =
    "INSERT OR REPLACE INTO relation_version (from_id, model, relation, to_id, checkpoint, removed) VALUES (?1, ?2, ?3, ?4, ?5, ?6)";

bool lessRelation(const RelationRecord& left, const RelationRecord& right)
{
    if (left.from.index != right.from.index) { return left.from.index < right.from.index; }
    if (left.relation != right.relation) { return left.relation < right.relation; }
    return left.to.index < right.to.index;
}

bool sameRelation(const RelationRecord& left, const RelationRecord& right)
{
    return left.from.index == right.from.index && left.relation == right.relation && left.to.index == right.to.index;
}

}

KiottySpecStatus RelationStore::load()
{
    _length = 0;
    auto query = _storage.prepare(literalText(LOAD_SQL));
    if (!query) { return query.code; }
    while (true) {
        const auto stepped = query.value.step();
        if (!stepped) { return _storage.check(stepped.code); }
        if (!stepped.value) { break; }
        const Row row = query.value.row();
        const auto from = _items.find(row.text(0));
        const auto to = _items.find(row.text(3));
        if (!from || !to) { continue; }
        if (_length == _records.size()) { return _storage.reject(KIOTTY_SPEC_STATUS_POOL_IS_FULL, {literalText("관계 수가 max_relations 를 넘음")}); }
        const uint32_t model = entity::parseKind(viewOf(row.text(1)));
        _records[_length++] = RelationRecord{from.value, to.value, static_cast<entity::ItemKind>(model), static_cast<uint32_t>(row.integer(2))};
    }
    std::sort(_records.begin(), _records.begin() + static_cast<std::ptrdiff_t>(_length), lessRelation);
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

size_t RelationStore::lowerBound(const RelationRecord& relation) const
{
    size_t low = 0;
    size_t high = _length;
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        if (lessRelation(_records[middle], relation)) { low = middle + 1; } else { high = middle; }
    }
    return low;
}

bool RelationStore::contains(const RelationRecord& relation) const
{
    const size_t position = lowerBound(relation);
    return position < _length && sameRelation(_records[position], relation);
}

KiottySpecStatus RelationStore::writeVersion(const RelationRecord& relation, bool removed, uint32_t sequence)
{
    const auto from = _items.get(relation.from);
    const auto to = _items.get(relation.to);
    if (!from || !to) { return KIOTTY_SPEC_STATUS_INVALID_HANDLE; }
    auto query = _storage.prepare(literalText(WRITE_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, from.value->id.view());
    query.value.bind(2, spanOf(entity::kindName(relation.model)));
    query.value.bind(3, static_cast<int64_t>(relation.relation));
    query.value.bind(4, to.value->id.view());
    query.value.bind(5, static_cast<int64_t>(sequence));
    query.value.bind(6, static_cast<int64_t>(removed ? 1 : 0));
    return _storage.check(query.value.run());
}

KiottySpecStatus RelationStore::add(const RelationRecord& relation, uint32_t sequence)
{
    const size_t position = lowerBound(relation);
    if (position < _length && sameRelation(_records[position], relation)) {
        return _storage.reject(KIOTTY_SPEC_STATUS_CONFLICT, {literalText("이미 있는 관계")});
    }
    if (_length == _records.size()) { return _storage.reject(KIOTTY_SPEC_STATUS_POOL_IS_FULL, {literalText("관계 수가 max_relations 에 닿음")}); }
    const KiottySpecStatus written = writeVersion(relation, false, sequence);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return written; }
    for (size_t index = _length; index > position; --index) { _records[index] = _records[index - 1]; }
    _records[position] = relation;
    ++_length;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus RelationStore::remove(const RelationRecord& relation, uint32_t sequence)
{
    const size_t position = lowerBound(relation);
    if (position == _length || !sameRelation(_records[position], relation)) {
        return _storage.reject(KIOTTY_SPEC_STATUS_NOT_FOUND, {literalText("없는 관계")});
    }
    const KiottySpecStatus written = writeVersion(_records[position], true, sequence);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return written; }
    for (size_t index = position + 1; index < _length; ++index) { _records[index - 1] = _records[index]; }
    --_length;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus RelationStore::removeAllOf(ItemHandle item, uint32_t sequence)
{
    size_t kept = 0;
    for (size_t index = 0; index < _length; ++index) {
        const RelationRecord relation = _records[index];
        if (relation.from.index != item.index && relation.to.index != item.index) {
            _records[kept++] = relation;
            continue;
        }
        const KiottySpecStatus written = writeVersion(relation, true, sequence);
        if (written != KIOTTY_SPEC_STATUS_SUCCESS) {
            for (size_t rest = index; rest < _length; ++rest) { _records[kept++] = _records[rest]; }
            _length = kept;
            return written;
        }
    }
    _length = kept;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

}
