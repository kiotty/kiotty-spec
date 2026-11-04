#pragma once

#include "implementation/base/meta.h"
#include "implementation/core/database.h"
#include "implementation/core/records.h"

#include <string>
#include <string_view>
#include <vector>

namespace kiotty::base {

// 변경 요청: 사용자 피드백과 이슈. 진입 단계부터 흐름을 다시 돌리게 한다.
class ChangeRequestStore {
public:
    ChangeRequestStore(core::Database &db, Meta &meta) : db_(db), meta_(meta) {}

    std::string create(KiottySpecChangeSource source, std::string_view body, int32_t entryStep);
    core::ChangeRequestRecord get(std::string_view id);
    void setState(std::string_view id, KiottySpecChangeState state, std::string_view note);
    void setEntryStep(std::string_view id, int32_t entryStep);
    std::vector<core::ChangeRequestRecord> list(KiottySpecChangeState stateOr0);
    size_t openCount();

private:
    core::Database &db_;
    Meta &meta_;
};

} // namespace kiotty::base
