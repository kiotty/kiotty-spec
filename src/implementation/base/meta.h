#pragma once

#include "implementation/core/database.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kiotty::base {

// 프로젝트 메타데이터, 정책, ID 발급기, 전역 쓰기 순번.
class Meta {
public:
    explicit Meta(core::Database &db) : db_(db) {}

    std::optional<std::string> get(std::string_view key);
    void set(std::string_view key, std::string_view value);

    std::optional<std::string> policy(std::string_view key);
    void setPolicy(std::string_view key, std::string_view value);
    int64_t policyInt(std::string_view key, int64_t fallback);

    std::string nextId(std::string_view prefix, int width);  // "REQ" -> "REQ-001"
    int64_t nextWriteSeq();

private:
    core::Database &db_;
};

} // namespace kiotty::base
