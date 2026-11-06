#pragma once

#include "implementation/data/repository/item_store.h"
#include "implementation/data/repository/meta.h"
#include "implementation/data/datasource/database.h"

#include <string>
#include <string_view>

namespace kiotty::spec::implementation {

class DecisionStore {
public:
    DecisionStore(Database &db, Meta &meta, ItemStore &items) : db_(db), meta_(meta), items_(items) {}

    std::string get(KiottySpecDecisionKind kind, int64_t cut = kCurrent);
    void rewrite(KiottySpecDecisionKind kind, std::string_view markdown);
    std::string check(KiottySpecDecisionKind kind);

    int64_t maxLine();
    int64_t maxConcerns();

private:
    std::string validate(std::string_view markdown, bool checkItems);

    Database &db_;
    Meta &meta_;
    ItemStore &items_;
};

}
