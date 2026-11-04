#pragma once

#include "implementation/base/item_store.h"
#include "implementation/base/meta.h"
#include "implementation/core/database.h"

#include <string>
#include <string_view>

namespace kiotty::base {

// 결정 기록: 종류당 문서 하나. 로그가 아니라 매 라운드 통째로 다시 쓴다 (R-7).
// 한 줄 형식: "- <항목 ID> · <관심사> : <결정> (이유) [기각: 대안 — 이유]"
class DecisionStore {
public:
    DecisionStore(core::Database &db, Meta &meta, ItemStore &items) : db_(db), meta_(meta), items_(items) {}

    std::string get(KiottySpecDecisionKind kind, int64_t cut = core::kCurrent);
    void rewrite(KiottySpecDecisionKind kind, std::string_view markdown);
    std::string check(KiottySpecDecisionKind kind);   // 문제 없으면 빈 문자열

    int64_t maxLine();
    int64_t maxConcerns();

private:
    std::string validate(std::string_view markdown, bool checkItems);

    core::Database &db_;
    Meta &meta_;
    ItemStore &items_;
};

} // namespace kiotty::base
