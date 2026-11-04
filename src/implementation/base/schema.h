#pragma once

#include "implementation/core/database.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace kiotty::base::schema {

inline constexpr int32_t kVersion = 1;

// 버전이 체크포인트마다 쌓이는 테이블과 그 키 열. 체크포인트 찍기·정리·jsonl 에서 공통으로 쓴다.
struct VersionedTable {
    std::string_view name;
    std::string_view keyColumns;  // "item_id" 또는 "from_id, to_id, type"
    bool hasRemoved;              // removed 열이 있는지
};

const std::vector<VersionedTable> &versionedTables();
const std::vector<std::string_view> &allTables();         // jsonl 내보내기 순서
std::string_view primaryKeyOrder(std::string_view table); // jsonl 정렬 기준

void ensure(core::Database &db);   // 없으면 만들고, 오래된 버전이면 올린다
int32_t version(core::Database &db);

} // namespace kiotty::base::schema
