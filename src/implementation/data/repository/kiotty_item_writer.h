#pragma once

#include "kiotty_spec.h"
#include "implementation/domain/entity/kiotty_failure.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/repository/kiotty_item_reader.h"
#include "implementation/data/repository/kiotty_item_tables.h"
#include "implementation/data/repository/kiotty_item_tags.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/entity/kiotty_version_records.h"

#include <cstdint>

namespace kiotty::spec::implementation {

// ItemStore 의 쓰는 쪽 (개발수칙 4.19). 쓰기는 sequence 체크포인트의 버전을 쓰거나 바꾸고 현재 뷰를 고친다.
// 파일에 옮기는 일은 호출자 (WriteScope) 가 한다.
class ItemWriter
{
public:
    ItemWriter(ItemTables& tables, entity::Failure& failure) : _t(tables), _failure(failure) {}

    // record.id 가 지운 항목의 것이라도 이미 있으면 CONFLICT
    Result<entity::ItemHandle, KiottySpecStatus> create(const entity::ItemRecord& record, uint32_t sequence);
    // title · summary · parent 를 바꾼 레코드로 새 버전을 쓴다. id 와 kind 는 바뀌지 않는다.
    KiottySpecStatus update(entity::ItemHandle handle, const entity::ItemRecord& record, uint32_t sequence);
    KiottySpecStatus setTag(entity::ItemHandle handle, uint32_t tag, uint64_t value, uint32_t sequence);
    KiottySpecStatus writeBody(entity::ItemHandle handle, Span<const char> body, uint32_t sequence);
    KiottySpecStatus remove(entity::ItemHandle handle, uint32_t sequence);
    // ID 만 바꾼다. 버전은 key 로 가리키므로 고치지 않는다. 지난 시점도 새 ID 로 보인다.
    KiottySpecStatus rename(entity::ItemHandle handle, const entity::IdText& id);

private:
    ItemReader reader() const { return ItemReader(_t); }
    ItemTags tags() const { return ItemTags(Span<const entity::TagRecord>(_t.tags.data(), _t.tag_count)); }
    KiottySpecStatus checkUnused(const entity::IdText& id);
    Result<uint32_t, KiottySpecStatus> newKey(const entity::IdText& id) const;
    // 현재 버전을 sequence 에 다시 쓴다. change 가 바꿀 필드를 채운다.
    KiottySpecStatus writeVersion(entity::ItemHandle handle, const entity::ItemVersion& version);
    Result<entity::ItemVersion, KiottySpecStatus> currentVersion(entity::ItemHandle handle) const;
    KiottySpecStatus applyTag(entity::ItemHandle handle, entity::ItemKind kind, uint32_t tag, uint64_t value);
    void dropTags(entity::ItemHandle handle);

    ItemTables& _t;
    entity::Failure& _failure;
};

}
