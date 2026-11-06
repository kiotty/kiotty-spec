#pragma once

#include "implementation/data/datasource/database.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kiotty::spec::implementation {

class Meta {
public:
    explicit Meta(Database &db) : db_(db) {}

    std::optional<std::string> get(std::string_view key);
    void set(std::string_view key, std::string_view value);

    std::optional<std::string> policy(std::string_view key);
    void setPolicy(std::string_view key, std::string_view value);
    int64_t policyInt(std::string_view key, int64_t fallback);

    std::string nextId(std::string_view prefix, int width);
    int64_t nextWriteSeq();

private:
    Database &db_;
};

}
