#pragma once

#include "kiotty_spec.h"
#include "implementation/core/kiotty_fixed_string.h"
#include "implementation/core/kiotty_result.h"
#include "implementation/core/kiotty_span.h"
#include "implementation/data/datasource/kiotty_storage.h"
#include "implementation/domain/entity/kiotty_item_records.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

constexpr size_t MAX_META_VALUE_LENGTH = 255;
using MetaValue = FixedString<MAX_META_VALUE_LENGTH>;

// meta 와 id_counter 테이블. 항목 ID 는 "<접두어>-<세 자리 이상 번호>" 로 만든다.
class MetaStore
{
public:
    explicit MetaStore(Storage& storage) : _storage(storage) {}

    KiottySpecStatus set(Span<const char> key, Span<const char> value);
    Result<MetaValue, KiottySpecStatus> get(Span<const char> key);
    Result<entity::IdText, KiottySpecStatus> nextId(Span<const char> prefix);
    // 가져온 ID 보다 카운터가 작으면 올린다.
    KiottySpecStatus observeId(Span<const char> id);

private:
    Storage& _storage;
};

}
