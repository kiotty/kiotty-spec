#pragma once

#include "kiotty_spec.h"
#include "implementation/domain/entity/kiotty_failure.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/core/kiotty_version_table.h"
#include "implementation/data/repository/kiotty_item_store.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/entity/kiotty_version_records.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

// 관계 버전 표와 현재 관계 풀 (설계서 v1 3.5, 10.1). 풀은 (from index, relation, to index) 순으로 정렬해 둔다.
class RelationStore
{
public:
    RelationStore(entity::Failure& failure, ItemStore& items, size_t capacity, size_t versions)
        : _failure(failure), _items(items), _versions(versions), _records(capacity), _order(versions)
    {
    }

    VersionTable<entity::RelationVersion>& versions() { return _versions; }
    // 관계마다 첫 버전의 index 를 (모델 Kind, from ID, relation, to ID) 순으로. 다음 호출 전까지만 유효하다.
    Span<const uint32_t> relationsInDiffOrder();
    // 항목 뷰를 먼저 만든 뒤에 부른다. 지금 없는 항목을 가리키는 관계는 건너뛴다.
    KiottySpecStatus rebuild();

    Span<const entity::RelationRecord> all() const { return Span<const entity::RelationRecord>(_records.data(), _length); }
    bool contains(const entity::RelationRecord& relation) const;

    KiottySpecStatus add(const entity::RelationRecord& relation, uint32_t sequence);
    KiottySpecStatus remove(const entity::RelationRecord& relation, uint32_t sequence);
    // 항목을 지울 때 그 항목이 주어나 목적어인 관계를 모두 지운다.
    KiottySpecStatus removeAllOf(entity::ItemHandle item, uint32_t sequence);

private:
    size_t lowerBound(const entity::RelationRecord& relation) const;
    KiottySpecStatus writeVersion(const entity::RelationRecord& relation, uint32_t removed, uint32_t sequence);

    entity::Failure& _failure;
    ItemStore& _items;
    VersionTable<entity::RelationVersion> _versions;
    std::vector<entity::RelationRecord> _records;
    size_t _length {0};
    std::vector<uint32_t> _order;
};

}
