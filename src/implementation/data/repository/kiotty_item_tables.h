#pragma once

#include "implementation/core/kiotty_fixed_map.h"
#include "implementation/core/kiotty_lazy_pool.h"
#include "implementation/core/kiotty_version_table.h"
#include "implementation/data/repository/kiotty_body_store.h"
#include "implementation/domain/entity/kiotty_item_records.h"
#include "implementation/domain/entity/kiotty_version_records.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kiotty::spec::implementation {

struct ItemCapacity {
    size_t items {};
    size_t tags {};
    size_t versions {};
    size_t tag_versions {};
    size_t body_bytes {};
};

// ItemStore 가 소유하는 표 묶음. 세부 클래스는 이것을 reference 로 받는다 (개발수칙 4.19).
// 버전 표 (names · versions · tag_versions) 가 파일 내용이고, 나머지는 거기서 만든 현재 뷰다.
struct ItemTables {
    explicit ItemTables(const ItemCapacity& capacity)
        : names(capacity.versions),
          versions(capacity.versions),
          tag_versions(capacity.tag_versions),
          items(capacity.items),
          id_index(capacity.items),
          key_index(capacity.items),
          keys(capacity.items, entity::NO_KEY),
          parents(capacity.items, entity::NO_KEY),
          tags(capacity.tags),
          order(capacity.versions),
          bodies(capacity.body_bytes, capacity.versions)
    {
    }

    // 파일 내용: key → ID, 항목 버전, 태그 버전, 현재 본문
    FixedMap<uint32_t, entity::IdText> names;
    VersionTable<entity::ItemVersion> versions;
    VersionTable<entity::TagVersion> tag_versions;

    // 현재 뷰 (설계서 v1 10.1). keys · parents 는 풀 index 마다 key 와 상위 항목 key 다.
    LazyPool<entity::ItemRecord> items;
    FixedMap<entity::IdText, uint32_t> id_index;
    FixedMap<uint32_t, uint32_t> key_index;
    std::vector<uint32_t> keys;
    std::vector<uint32_t> parents;
    std::vector<entity::TagRecord> tags;
    size_t tag_count {0};

    // 정렬할 때 빌려 쓰는 key 배열
    std::vector<uint32_t> order;
    BodyStore bodies;
};

}
