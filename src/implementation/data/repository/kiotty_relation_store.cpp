#include "implementation/data/repository/kiotty_relation_store.h"

#include "implementation/core/kiotty_text_view.h"

#include <algorithm>

namespace kiotty::spec::implementation {

namespace {

using entity::ItemHandle;
using entity::RelationRecord;
using entity::RelationVersion;

constexpr size_t BISECTION_DIVISOR = 2;

size_t collectFirstRelations(Span<uint32_t> order, Span<const RelationVersion> versions);
bool isDistinctAt(Span<const RelationVersion> versions, size_t neighbor_index,
                  const RelationVersion& version);
void sortForDiff(Span<uint32_t> firsts, Span<const RelationVersion> versions,
                 const ItemTables& tables);
RelationVersion readVersion(Span<const RelationVersion> versions, uint32_t index);
bool isStoredAt(Span<const RelationRecord> used, size_t position, const RelationRecord& relation);
size_t findLowerBound(Span<const RelationRecord> used, const RelationRecord& relation);
size_t insertRelation(Span<RelationRecord> records, size_t length, size_t position,
                      const RelationRecord& relation);
size_t removeRelation(Span<RelationRecord> used, size_t position);
bool isRelationBefore(const RelationRecord& left, const RelationRecord& right);
bool isSameRelation(const RelationRecord& left, const RelationRecord& right);

}

KiottySpecStatus RelationStore::rebuild()
{
    _length = 0;
    const Span<const RelationVersion> versions = _versions.all();
    const size_t length = versions.length();
    const Span<RelationRecord> records = viewRecords();
    for (size_t index = 0; index < length; ++index) {
        const RelationVersion& version = versions[index];
        const bool is_superseded =
            !isDistinctAt(versions, index + 1, version) || version.removed != 0;
        if (is_superseded) { continue; }
        const auto from = _items.reader().findKey(version.from);
        const auto to = _items.reader().findKey(version.to);
        const bool is_dangling = !from || !to;
        if (is_dangling) { continue; }
        const Result<RelationRecord&, KiottySpecStatus> slot = records.at(_length);
        if (!slot) {
            return _failure.raise(KIOTTY_SPEC_STATUS_POOL_IS_FULL,
                                  KIOTTY_SPEC_SUBJECT_CONFIGURATION, literalText("max_relations"));
        }
        *slot.value = RelationRecord{from.value, to.value, version.model, version.relation};
        ++_length;
    }
    const Span<RelationRecord> used(records.begin(), _length);
    std::sort(used.begin(), used.end(), isRelationBefore);
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

Span<const uint32_t> RelationStore::relationsInDiffOrder()
{
    const Span<const RelationVersion> versions = _versions.all();
    const Span<uint32_t> order(_order.data(), _order.size());
    const Span<uint32_t> firsts(order.begin(), collectFirstRelations(order, versions));
    sortForDiff(firsts, versions, _items.tables());
    return firsts;
}

bool RelationStore::contains(const RelationRecord& relation) const
{ return isStoredAt(all(), lowerBound(relation), relation); }

KiottySpecStatus RelationStore::add(const RelationRecord& relation, uint32_t sequence)
{
    const size_t position = lowerBound(relation);
    const bool is_duplicate = isStoredAt(all(), position, relation);
    if (is_duplicate) {
        return _failure.raise(KIOTTY_SPEC_STATUS_CONFLICT, KIOTTY_SPEC_SUBJECT_RELATION);
    }
    const Span<RelationRecord> records = viewRecords();
    const Result<RelationRecord&, KiottySpecStatus> spare = records.at(_length);
    if (!spare) {
        return _failure.raise(KIOTTY_SPEC_STATUS_POOL_IS_FULL, KIOTTY_SPEC_SUBJECT_CONFIGURATION,
                              literalText("max_relations"));
    }
    const KiottySpecStatus written = writeVersion(relation, 0, sequence);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return written; }
    _length = insertRelation(records, _length, position, relation);
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus RelationStore::remove(const RelationRecord& relation, uint32_t sequence)
{
    const size_t position = lowerBound(relation);
    const Result<const RelationRecord&, KiottySpecStatus> found = all().at(position);
    const bool is_missing = !found || !isSameRelation(*found.value, relation);
    if (is_missing) {
        return _failure.raise(KIOTTY_SPEC_STATUS_NOT_FOUND, KIOTTY_SPEC_SUBJECT_RELATION);
    }
    const KiottySpecStatus written = writeVersion(*found.value, 1, sequence);
    if (written != KIOTTY_SPEC_STATUS_SUCCESS) { return written; }
    const Span<RelationRecord> used(viewRecords().begin(), _length);
    _length = removeRelation(used, position);
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus RelationStore::removeAllOf(ItemHandle item, uint32_t sequence)
{
    const Span<RelationRecord> used(viewRecords().begin(), _length);
    const size_t length = used.length();
    size_t kept = 0;
    for (size_t index = 0; index < length; ++index) {
        const RelationRecord relation = used[index];
        const bool is_kept = relation.from.index != item.index && relation.to.index != item.index;
        const Result<RelationRecord&, KiottySpecStatus> slot = used.at(kept);
        if (is_kept && slot) {
            *slot.value = relation;
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
{ return findLowerBound(all(), relation); }

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

size_t collectFirstRelations(Span<uint32_t> order, Span<const RelationVersion> versions)
{
    size_t count = 0;
    const size_t length = versions.length();
    for (size_t index = 0; index < length; ++index) {
        const bool is_first = index == 0 || isDistinctAt(versions, index - 1, versions[index]);
        const Result<uint32_t&, KiottySpecStatus> slot = order.at(count);
        if (is_first && slot) {
            *slot.value = static_cast<uint32_t>(index);
            ++count;
        }
    }
    return count;
}

bool isDistinctAt(Span<const RelationVersion> versions, size_t neighbor_index,
                  const RelationVersion& version)
{
    const Result<const RelationVersion&, KiottySpecStatus> neighbor = versions.at(neighbor_index);
    if (!neighbor) { return true; }
    const RelationVersion& other = *neighbor.value;
    return !(other.identity() == version.identity());
}

void sortForDiff(Span<uint32_t> firsts, Span<const RelationVersion> versions,
                 const ItemTables& tables)
{
    std::sort(firsts.begin(), firsts.end(), [&tables, versions](uint32_t left, uint32_t right) {
        const RelationVersion a = readVersion(versions, left);
        const RelationVersion b = readVersion(versions, right);
        if (a.model != b.model) {
            return static_cast<uint32_t>(a.model) < static_cast<uint32_t>(b.model);
        }
        const auto a_from = tables.names.find(a.from).value;
        const auto b_from = tables.names.find(b.from).value;
        if (!(a_from == b_from)) { return a_from < b_from; }
        if (a.relation != b.relation) { return a.relation < b.relation; }
        return tables.names.find(a.to).value < tables.names.find(b.to).value;
    });
}

RelationVersion readVersion(Span<const RelationVersion> versions, uint32_t index)
{
    const Result<const RelationVersion&, KiottySpecStatus> version = versions.at(index);
    return version ? *version.value : RelationVersion{};
}

bool isStoredAt(Span<const RelationRecord> used, size_t position, const RelationRecord& relation)
{
    const Result<const RelationRecord&, KiottySpecStatus> stored = used.at(position);
    return stored && isSameRelation(*stored.value, relation);
}

size_t findLowerBound(Span<const RelationRecord> used, const RelationRecord& relation)
{
    size_t low = 0;
    size_t high = used.length();
    while (low < high) {
        const size_t middle = low + (high - low) / BISECTION_DIVISOR;
        const Result<const RelationRecord&, KiottySpecStatus> probe = used.at(middle);
        const bool is_less = probe && isRelationBefore(*probe.value, relation);
        if (is_less) { low = middle + 1; } else { high = middle; }
    }
    return low;
}

size_t insertRelation(Span<RelationRecord> records, size_t length, size_t position,
                      const RelationRecord& relation)
{
    std::copy_backward(records.begin() + position, records.begin() + length,
                       records.begin() + length + 1);
    const Result<RelationRecord&, KiottySpecStatus> slot = records.at(position);
    if (!slot) { return length; }
    *slot.value = relation;
    return length + 1;
}

size_t removeRelation(Span<RelationRecord> used, size_t position)
{
    std::copy(used.begin() + position + 1, used.end(), used.begin() + position);
    return used.length() - 1;
}

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
