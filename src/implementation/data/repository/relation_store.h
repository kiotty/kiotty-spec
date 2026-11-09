#pragma once

#include "kiotty_spec.h"
#include "implementation/core/result.h"
#include "implementation/core/span.h"
#include "implementation/data/datasource/storage.h"
#include "implementation/data/repository/item_store.h"
#include "implementation/domain/entity/item_records.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

// 관계 풀 (설계서 v1 3.5, 10.1). (from index, relation, to index) 순으로 정렬해 둔다.
class RelationStore
{
public:
    RelationStore(Storage& storage, ItemStore& items, size_t capacity) : _storage(storage), _items(items), _records(capacity) {}

    // 항목 풀을 먼저 적재한 뒤에 부른다. 지금 없는 항목을 가리키는 관계는 건너뛴다.
    KiottySpecStatus load();

    Span<const entity::RelationRecord> all() const { return Span<const entity::RelationRecord>(_records.data(), _length); }
    bool contains(const entity::RelationRecord& relation) const;

    KiottySpecStatus add(const entity::RelationRecord& relation, uint32_t sequence);
    KiottySpecStatus remove(const entity::RelationRecord& relation, uint32_t sequence);
    // 항목을 지울 때 그 항목이 주어나 목적어인 관계를 모두 지운다.
    KiottySpecStatus removeAllOf(entity::ItemHandle item, uint32_t sequence);

private:
    size_t lowerBound(const entity::RelationRecord& relation) const;
    KiottySpecStatus writeVersion(const entity::RelationRecord& relation, bool removed, uint32_t sequence);

    Storage& _storage;
    ItemStore& _items;
    std::vector<entity::RelationRecord> _records;
    size_t _length {0};
};

}
