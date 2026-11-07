#pragma once

#include "implementation/data/datasource/storage.h"
#include "implementation/data/repository/store_result.h"

#include <cstdint>
#include <string_view>

namespace kiotty::spec::implementation {

class Meta {
public:
    explicit Meta(Storage &storage) : _storage(storage) {}

    OptionalTextResult get(std::string_view key);
    KiottySpecStatus set(std::string_view key, std::string_view value);
    OptionalTextResult policy(std::string_view key);
    KiottySpecStatus setPolicy(std::string_view key, std::string_view value);
    SequenceResult policyInt(std::string_view key, int64_t fallback);
    TextResult nextId(std::string_view prefix, int width);
    SequenceResult nextWriteSeq();

private:
    SequenceResult counter(std::string_view prefix);

    Storage &_storage;
};

}
