#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/repository/kiotty_item_tables.h"
#include "implementation/domain/entity/kiotty_item_records.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

// ItemStore 의 현재 뷰를 읽는 쪽 (개발수칙 4.19).
class ItemReader
{
public:
    explicit ItemReader(ItemTables& tables) : _t(tables) {}

    Result<entity::ItemHandle, KiottySpecStatus> find(Span<const char> id) const;
    Result<entity::ItemHandle, KiottySpecStatus> findKey(uint32_t key) const;
    Result<const entity::ItemRecord*, KiottySpecStatus> get(entity::ItemHandle handle) const;
    entity::ItemHandle handleAt(size_t index) const { return _t.items.handleAt(index); }
    size_t capacity() const { return _t.items.capacity(); }
    // 핸들이 무효이면 NO_KEY
    uint32_t key(entity::ItemHandle handle) const;
    // ID 순서로 훑을 때 쓴다.
    size_t count() const { return _t.id_index.length(); }
    entity::ItemHandle handleInIdOrder(size_t position) const { return _t.items.handleAt(_t.id_index.valueAt(position)); }

    // 현재 본문을 buffer 에 null 종료로 복사한다. 작으면 CAPACITY_EXCEEDED 와 필요한 바이트 수
    Result<size_t, KiottySpecStatus> readBody(entity::ItemHandle handle, char* buffer, size_t capacity) const;

private:
    ItemTables& _t;
};

}
