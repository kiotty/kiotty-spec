#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_fixed_map.h"
#include "implementation/core/kiotty_lazy_pool.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_storage.h"
#include "implementation/data/repository/kiotty_item_reader.h"
#include "implementation/data/repository/kiotty_item_tags.h"
#include "implementation/data/repository/kiotty_item_writer.h"
#include "implementation/domain/entity/kiotty_item_records.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

// 항목 풀 + 태그 풀 + ID 색인 (설계서 v1 10.1). 본문은 풀에 두지 않고 DB 에서만 읽는다 (3.7).
// 멤버를 소유하고, 기능은 세부 클래스에 나눠 맡긴다 (개발수칙 4.19). 세부 클래스는 부른 자리에서 쓰고 버린다.
class ItemStore
{
public:
    ItemStore(Storage& storage, size_t max_items, size_t max_tags);

    // DB 의 현재 버전을 풀에 다시 적재한다. 이전 핸들은 모두 무효가 된다.
    KiottySpecStatus load();

    ItemReader reader() { return ItemReader(_storage, _items, _id_index); }
    ItemTags tags() const { return ItemTags(Span<const entity::TagRecord>(_tags.data(), _tag_count)); }
    ItemWriter writer() { return ItemWriter(_storage, _items, _id_index, _tags, _tag_count); }

private:
    KiottySpecStatus loadRow(const Row& row);
    KiottySpecStatus loadTags(entity::ItemHandle handle, entity::ItemKind kind, Span<const char> id, Span<const char> text);
    KiottySpecStatus linkParents();

    Storage& _storage;
    LazyPool<entity::ItemRecord> _items;
    FixedMap<entity::IdText, uint32_t> _id_index;
    std::vector<entity::TagRecord> _tags;
    size_t _tag_count {0};
};

}
