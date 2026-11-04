#pragma once

#include "implementation/base/item_store.h"
#include "implementation/core/database.h"
#include "implementation/core/records.h"

#include <optional>
#include <string_view>
#include <vector>

namespace kiotty::base {

struct ResultSummary {
    size_t total = 0;
    size_t pass = 0;
    size_t fail = 0;
    size_t notRun = 0;
};

// 결과: 테스트 실행과 코드 검사(대상이 개발 규칙)의 PASS / FAIL.
class ResultStore {
public:
    ResultStore(core::Database &db, ItemStore &items) : db_(db), items_(items) {}

    void record(std::string_view targetId, KiottySpecTestResult result, std::string_view log);
    std::optional<core::ResultRecord> last(std::string_view targetId);
    std::vector<core::ResultRecord> history(std::string_view targetId);
    std::vector<core::ItemRecord> failed(KiottySpecItemKind kindOr0);
    ResultSummary summary(KiottySpecItemKind kind);

private:
    core::Database &db_;
    ItemStore &items_;
};

} // namespace kiotty::base
