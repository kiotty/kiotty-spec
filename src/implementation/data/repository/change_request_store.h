#pragma once

#include "implementation/data/datasource/storage.h"
#include "implementation/data/repository/meta.h"
#include "implementation/data/repository/store_result.h"

#include <string_view>

namespace kiotty::spec::implementation {

class ChangeRequestStore {
public:
    ChangeRequestStore(Storage &storage, Meta &meta) : _storage(storage), _meta(meta) {}

    TextResult create(KiottySpecChangeSource source, std::string_view body, int32_t entryStep);
    ChangeRequestResult get(std::string_view id);
    KiottySpecStatus setState(std::string_view id, KiottySpecChangeState state, std::string_view note);
    KiottySpecStatus setEntryStep(std::string_view id, int32_t entryStep);
    ChangeRequestListResult list(KiottySpecChangeState stateOr0);
    CountResult openCount();

private:
    KiottySpecStatus validate(KiottySpecChangeSource source, std::string_view body, int32_t entryStep);
    KiottySpecStatus checkStep(int32_t entryStep);
    KiottySpecStatus insert(std::string_view id, KiottySpecChangeSource source, std::string_view body, int32_t entryStep);

    Storage &_storage;
    Meta &_meta;
};

}
