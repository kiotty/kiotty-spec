#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_fixed_map.h"
#include "implementation/core/kiotty_lazy_pool.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_storage.h"
#include "implementation/data/repository/kiotty_item_reader.h"
#include "implementation/data/repository/kiotty_item_tags.h"
#include "implementation/domain/entity/kiotty_item_records.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

// ItemStore 의 쓰는 쪽 (개발수칙 4.19). 멤버는 ItemStore 가 소유하고 여기서는 reference 만 든다.
// 쓰기는 "현재 체크포인트의 버전 행"을 통째로 다시 쓰고, 성공하면 풀을 고친다.
class ItemWriter
{
public:
    ItemWriter(Storage& storage, LazyPool<entity::ItemRecord>& items, FixedMap<entity::IdText, uint32_t>& id_index,
               std::vector<int64_t>& keys, std::vector<entity::TagRecord>& tags, size_t& tag_count)
        : _storage(storage), _items(items), _id_index(id_index), _keys(keys), _tags(tags), _tag_count(tag_count)
    {
    }

    // record.id 가 item 표에 있으면 (지운 항목 포함) CONFLICT
    Result<entity::ItemHandle, KiottySpecStatus> create(const entity::ItemRecord& record, uint32_t sequence);
    // ID 만 바꾼다. 버전 행과 관계 행은 key 로 가리키므로 고치지 않는다. 지난 시점도 새 ID 로 보인다.
    KiottySpecStatus rename(entity::ItemHandle handle, const entity::IdText& id);
    // title · summary · parent 를 바꾼 레코드로 새 버전을 쓴다. id 와 kind 는 바뀌지 않는다.
    KiottySpecStatus update(entity::ItemHandle handle, const entity::ItemRecord& record, uint32_t sequence);
    KiottySpecStatus setTag(entity::ItemHandle handle, uint32_t tag, uint64_t value, uint32_t sequence);
    KiottySpecStatus writeBody(entity::ItemHandle handle, Span<const char> body, uint32_t sequence);
    KiottySpecStatus remove(entity::ItemHandle handle, uint32_t sequence);

private:
    struct TagEdit {
        bool apply;
        uint32_t tag;
        uint64_t value;
    };

    ItemReader reader() const { return ItemReader(_storage, _items, _id_index, _keys); }
    // item 표에 ID 가 있으면 CONFLICT 를 남긴다.
    KiottySpecStatus checkUnused(const entity::IdText& id);
    ItemTags tags() const { return ItemTags(Span<const entity::TagRecord>(_tags.data(), _tag_count)); }
    KiottySpecStatus writeVersion(int64_t key, const entity::ItemRecord& record, TagEdit edit, const Span<const char>* body, bool removed, uint32_t sequence);
    KiottySpecStatus applyTag(entity::ItemHandle handle, entity::ItemKind kind, uint32_t tag, uint64_t value);
    void dropTags(entity::ItemHandle handle);

    Storage& _storage;
    LazyPool<entity::ItemRecord>& _items;
    FixedMap<entity::IdText, uint32_t>& _id_index;
    std::vector<int64_t>& _keys;
    std::vector<entity::TagRecord>& _tags;
    size_t& _tag_count;
};

}
