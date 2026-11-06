#pragma once

#include "implementation/data/repository/meta.h"
#include "implementation/data/datasource/database.h"
#include "implementation/domain/entity/records.h"

#include <string>
#include <string_view>
#include <vector>

namespace kiotty::spec::implementation {

class ChangeRequestStore {
public:
    ChangeRequestStore(Database &db, Meta &meta) : db_(db), meta_(meta) {}

    std::string create(KiottySpecChangeSource source, std::string_view body, int32_t entryStep);
    ChangeRequestRecord get(std::string_view id);
    void setState(std::string_view id, KiottySpecChangeState state, std::string_view note);
    void setEntryStep(std::string_view id, int32_t entryStep);
    std::vector<ChangeRequestRecord> list(KiottySpecChangeState stateOr0);
    size_t openCount();

private:
    Database &db_;
    Meta &meta_;
};

}
