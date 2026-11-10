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

// ItemStore 의 읽는 쪽 (개발수칙 4.19). 멤버는 ItemStore 가 소유하고 여기서는 reference 만 든다.
class ItemReader
{
public:
    ItemReader(Storage& storage, LazyPool<entity::ItemRecord>& items, const FixedMap<entity::IdText, uint32_t>& id_index,
               const std::vector<int64_t>& keys)
        : _storage(storage), _items(items), _id_index(id_index), _keys(keys)
    {
    }

    Result<entity::ItemHandle, KiottySpecStatus> find(Span<const char> id) const;
    Result<const entity::ItemRecord*, KiottySpecStatus> get(entity::ItemHandle handle) const;
    entity::ItemHandle handleAt(size_t index) const { return _items.handleAt(index); }
    // DB 의 item.key. 핸들이 무효이면 0
    int64_t key(entity::ItemHandle handle) const;
    size_t capacity() const { return _items.capacity(); }
    // ID 순서로 훑을 때 쓴다.
    size_t count() const { return _id_index.length(); }
    entity::ItemHandle handleInIdOrder(size_t position) const { return _items.handleAt(_id_index.valueAt(position)); }

    // 현재 본문을 buffer 에 null 종료로 복사한다. 작으면 CAPACITY_EXCEEDED 와 필요한 바이트 수
    Result<size_t, KiottySpecStatus> readBody(entity::ItemHandle handle, char* buffer, size_t capacity) const;

private:
    Storage& _storage;
    LazyPool<entity::ItemRecord>& _items;
    const FixedMap<entity::IdText, uint32_t>& _id_index;
    const std::vector<int64_t>& _keys;
};

}
