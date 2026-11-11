#include "implementation/data/repository/kiotty_relation_store.h"

#include "implementation/core/kiotty_text_view.h"

#include <algorithm>

namespace kiotty::spec::implementation {

namespace {

using entity::ItemHandle;
using entity::RelationRecord;
using entity::RelationVersion;

constexpr size_t BISECTION_DIVISOR = 2;

bool isRelationBefore(const RelationRecord& left, const RelationRecord& right);
bool isSameRelation(const RelationRecord& left, const RelationRecord& right);

}

KiottySpecStatus RelationStore::rebuild()
{
    _length = 0;
    const Span<const RelationVersion> versions = _versions.all();
    const size_t length = versions.length();
    const size_t capacity = _records.size();
    for (size_t index = 0; index < length; ++index) {
        const RelationVersion& version = versions[index];
        const bool last = index + 1 == length ||
                          !(versions[index + 1].identity() == version.identity());
        if (!last || version.removed != 0) { continue; }
        const auto from = _items.reader().findKey(version.from);
        const auto to = _items.reader().findKey(version.to);
        if (!from || !to) { continue; }
        if (_length == capacity) {
            return _failure.raise(KIOTTY_SPEC_STATUS_POOL_IS_FULL,
                                  KIOTTY_SPEC_SUBJECT_CONFIGURATION, literalText("max_relations"));
        }
        _records[_length] = RelationRecord{from.value, to.value, version.model, version.relation};
        ++_length;
    }
    std::sort(_records.begin(), _records.begin() + static_cast<std::ptrdiff_t>(_length),
              isRelationBefore);
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Span<const uint32_t> RelationStore::relationsInDiffOrder()
{
    const Span<const RelationVersion> versions = _versions.all();
    size_t count = 0;
    const size_t length = versions.length();
    for (size_t index = 0; index < length; ++index) {
        const bool first =
            index == 0 || !(versions[index - 1].identity() == versions[index].identity());
        if (!first) { continue; }
        _order[count] = static_cast<uint32_t>(index);
        ++count;
    }
    const ItemTables& tables = _items.tables();
    std::sort(_order.begin(), _order.begin() + static_cast<std::ptrdiff_t>(count),
              [&tables, versions](uint32_t left, uint32_t right) {
                  const RelationVersion& a = versions[left];
                  const RelationVersion& b = versions[right];
                  if (a.model != b.model) {
                      return static_cast<uint32_t>(a.model) < static_cast<uint32_t>(b.model);
                  }
                  const auto a_from = tables.names.find(a.from).value;
                  const auto b_from = tables.names.find(b.from).value;
                  if (!(a_from == b_from)) { return a_from < b_from; }
                  if (a.relation != b.relation) { return a.relation < b.relation; }
                  return tables.names.find(a.to).value < tables.names.find(b.to).value;
              });
    return Span<const uint32_t>(_order.data(), count);
}

bool RelationStore::contains(const RelationRecord& relation) const
{
    const size_t position = lowerBound(relation);
    return position < _length && isSameRelation(_records[position], relation);
}

KiottySpecStatus RelationStore::add(const RelationRecord& relation, uint32_t sequence)
{
    const size_t position = lowerBound(relation);
    bool is_duplicate = false;
    if (position < _length) { is_duplicate = isSameRelation(_records[position], relation); }
    if (is_duplicate) {
        return _failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, KIOTTY_SPEC_SUBJECT_RELATION);
    }
    const size_t capacity = _records.size();
    if (_length == capacity) {
        return _failure.raise(KIOTTY_SPEC_STATUS_POOL_IS_FULL, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                              literalText("max_relations"));
    }
    const KiottySpecStatus written = writeVersion(relation, 0, sequence);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return written; }
    for (size_t index = _length; index > position; --index) {
        _records[index] = _records[index - 1];
    }
    _records[position] = relation;
    ++_length;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus RelationStore::remove(const RelationRecord& relation, uint32_t sequence)
{
    const size_t position = lowerBound(relation);
    bool is_found = false;
    if (position != _length) { is_found = isSameRelation(_records[position], relation); }
    if (!is_found) {
        return _failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, KIOTTY_SPEC_SUBJECT_RELATION);
    }
    const KiottySpecStatus written = writeVersion(_records[position], 1, sequence);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return written; }
    for (size_t index = position + 1; index < _length; ++index) {
        _records[index - 1] = _records[index];
    }
    --_length;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus RelationStore::removeAllOf(ItemHandle item, uint32_t sequence)
{
    size_t kept = 0;
    for (size_t index = 0; index < _length; ++index) {
        const RelationRecord relation = _records[index];
        if (relation.from.index != item.index && relation.to.index != item.index) {
            _records[kept] = relation;
            ++kept;
            continue;
        }
        const KiottySpecStatus written = writeVersion(relation, 1, sequence);
        if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return written; }
    }
    _length = kept;
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

size_t RelationStore::lowerBound(const RelationRecord& relation) const
{
    size_t low = 0;
    size_t high = _length;
    while (low < high) {
        const size_t middle = low + (high - low) / BISECTION_DIVISOR;
        const bool is_less = isRelationBefore(_records[middle], relation);
        if (is_less) { low = middle + 1; } else { high = middle; }
    }
    return low;
}

KiottySpecStatus RelationStore::writeVersion(const RelationRecord& relation, uint32_t removed,
                                             uint32_t sequence)
{
    const uint32_t from = _items.reader().key(relation.from);
    const uint32_t to = _items.reader().key(relation.to);
    if (from == entity::NO_KEY || to == entity::NO_KEY) {
        return KIOTTY_SPEC_STATUS_INVALID_HANDLE;
    }
    const RelationVersion version {from, relation.relation, to, sequence, removed, relation.model};
    const KiottySpecStatus written = _versions.write(version);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) {
        return _failure.raise(written, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                              literalText("max_relations"));
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

namespace {

bool isRelationBefore(const RelationRecord& left, const RelationRecord& right)
{
    if (left.from.index != right.from.index) { return left.from.index < right.from.index; }
    if (left.relation != right.relation) { return left.relation < right.relation; }
    return left.to.index < right.to.index;
}

bool isSameRelation(const RelationRecord& left, const RelationRecord& right)
{
    return left.from.index == right.from.index && left.relation == right.relation &&
           left.to.index == right.to.index;
}

}

}
