#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_fixed_map.h"
#include "implementation/core/kiotty_lazy_pool.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_storage.h"
#include "implementation/domain/entity/kiotty_item_records.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

// 항목 풀 + 태그 풀 + ID 색인 (설계서 v1 10.1). 본문은 풀에 두지 않고 DB 에서만 읽는다 (3.7).
// 쓰기는 "현재 체크포인트의 버전 행"을 통째로 다시 쓰고, 성공하면 풀을 고친다.
class ItemStore
{
public:
    ItemStore(Storage& storage, size_t max_items, size_t max_tags);

    // DB 의 현재 버전을 풀에 다시 적재한다. 이전 핸들은 모두 무효가 된다.
    KiottySpecStatus load();

    Result<entity::ItemHandle, KiottySpecStatus> find(Span<const char> id) const;
    Result<const entity::ItemRecord*, KiottySpecStatus> get(entity::ItemHandle handle);
    entity::ItemHandle handleAt(size_t index) const { return _items.handleAt(index); }
    size_t capacity() const { return _items.capacity(); }
    // ID 순서로 훑을 때 쓴다.
    size_t count() const { return _id_index.length(); }
    entity::ItemHandle handleInIdOrder(size_t position) const { return _items.handleAt(_id_index.valueAt(position)); }

    // (item index, tag) 순으로 정렬된 태그 레코드
    Span<const entity::TagRecord> allTags() const { return Span<const entity::TagRecord>(_tags.data(), _tag_count); }
    Span<const entity::TagRecord> tagsOf(entity::ItemHandle handle) const;
    uint64_t tagValue(entity::ItemHandle handle, uint32_t tag) const;

    Result<entity::ItemHandle, KiottySpecStatus> create(const entity::ItemRecord& record, uint32_t sequence);
    // title · summary · parent 를 바꾼 레코드로 새 버전을 쓴다. id 와 kind 는 바뀌지 않는다.
    KiottySpecStatus update(entity::ItemHandle handle, const entity::ItemRecord& record, uint32_t sequence);
    KiottySpecStatus setTag(entity::ItemHandle handle, uint32_t tag, uint64_t value, uint32_t sequence);
    KiottySpecStatus writeBody(entity::ItemHandle handle, Span<const char> body, uint32_t sequence);
    KiottySpecStatus remove(entity::ItemHandle handle, uint32_t sequence);

    // 현재 본문을 buffer 에 null 종료로 복사한다. 작으면 CAPACITY_EXCEEDED 와 필요한 바이트 수
    Result<size_t, KiottySpecStatus> readBody(entity::ItemHandle handle, char* buffer, size_t capacity);

private:
    struct TagEdit {
        bool apply;
        uint32_t tag;
        uint64_t value;
    };

    KiottySpecStatus writeVersion(const entity::ItemRecord& record, TagEdit edit, const Span<const char>* body, bool removed, uint32_t sequence);
    KiottySpecStatus applyTag(entity::ItemHandle handle, entity::ItemKind kind, uint32_t tag, uint64_t value);
    void dropTags(entity::ItemHandle handle);
    size_t lowerBound(uint32_t index, uint32_t tag) const;

    Storage& _storage;
    LazyPool<entity::ItemRecord> _items;
    FixedMap<entity::IdText, uint32_t> _id_index;
    std::vector<entity::TagRecord> _tags;
    size_t _tag_count {0};
};

}
