#pragma once

#include "kiotty_spec.h"
#include "implementation/domain/entity/kiotty_failure.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/repository/kiotty_item_history.h"
#include "implementation/data/repository/kiotty_item_reader.h"
#include "implementation/data/repository/kiotty_item_tables.h"
#include "implementation/data/repository/kiotty_item_tags.h"
#include "implementation/data/repository/kiotty_item_writer.h"

#include <cstddef>

namespace kiotty::spec::implementation {

// 항목 버전 표와 현재 뷰 (항목 풀 + 태그 풀 + ID 색인, 설계서 v1 10.1). 본문은 풀에 두지 않고 BodyStore 에서만 읽는다 (3.7).
// 표를 소유하고, 기능은 세부 클래스에 나눠 맡긴다 (개발수칙 4.19). 세부 클래스는 부른 자리에서 쓰고 버린다.
class ItemStore
{
public:
    ItemStore(entity::Failure& failure, const ItemCapacity& capacity) : _failure(failure), _t(capacity) {}

    // 버전 표를 비운다. 파일을 적재하기 전에 부른다.
    void clear();
    // 버전 표에서 현재 뷰를 다시 만든다. 이전 핸들은 모두 무효가 된다.
    KiottySpecStatus rebuild();

    ItemTables& tables() { return _t; }
    ItemHistory history() { return ItemHistory(_t); }
    ItemReader reader() { return ItemReader(_t); }
    ItemTags tags() const { return ItemTags(Span<const entity::TagRecord>(_t.tags.data(), _t.tag_count)); }
    ItemWriter writer() { return ItemWriter(_t, _failure); }

private:
    size_t collectLatest();
    KiottySpecStatus place(const entity::ItemVersion& version);
    void linkParents();
    KiottySpecStatus loadTags();

    entity::Failure& _failure;
    ItemTables _t;
};

}
