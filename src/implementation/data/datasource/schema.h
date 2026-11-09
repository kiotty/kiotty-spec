#pragma once

#include "kiotty_spec.h"
#include "implementation/core/result.h"
#include "implementation/core/span.h"
#include "implementation/data/datasource/storage.h"

#include <cstdint>
#include <string_view>

namespace kiotty::spec::implementation {

// 1: 정의서 v0 (category, 링크 타입, 파일, 변경 요청 · 결과 테이블). 2: 설계서 v1.
inline constexpr int32_t SCHEMA_VERSION = 2;
inline constexpr int32_t LEGACY_SCHEMA_VERSION = 1;

// 체크포인트로 버전을 나누는 테이블. 정리(Compaction)에서 쓴다.
struct VersionedTable {
    std::string_view name;
    std::string_view keep_latest_sql;
    std::string_view delete_older_sql;
    std::string_view delete_removed_sql;
};

// jsonl 로 내보내는 테이블과 줄 순서
struct OrderedTable {
    std::string_view name;
    std::string_view key_order;
};

Span<const VersionedTable> versionedTables();
Span<const OrderedTable> orderedTables();

// 빈 DB 에 스키마를 만들거나 이미 있는 스키마 버전을 확인한다. 옛 버전이면 LEGACY_SCHEMA_VERSION 을 돌려준다.
Result<int32_t, KiottySpecStatus> ensure(Storage& storage);
// 모든 테이블을 지우고 빈 스키마를 만든다. jsonl 가져오기가 쓴다.
KiottySpecStatus recreate(Storage& storage);

}
