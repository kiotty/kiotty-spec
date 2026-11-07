#pragma once

#include "implementation/data/datasource/storage.h"
#include "implementation/data/repository/item_store.h"
#include "implementation/data/repository/meta.h"
#include "implementation/data/repository/store_result.h"

#include <string_view>

namespace kiotty::spec::implementation {

class DecisionStore {
public:
    DecisionStore(Storage &storage, Meta &meta, ItemStore &items) : _storage(storage), _meta(meta), _items(items) {}

    TextResult get(KiottySpecDecisionKind kind, int64_t cut = kCurrent);
    KiottySpecStatus rewrite(KiottySpecDecisionKind kind, std::string_view markdown);
    TextResult check(KiottySpecDecisionKind kind);
    SequenceResult maxLine();
    SequenceResult maxConcerns();

private:
    TextResult validate(std::string_view markdown, bool checkItems);
    KiottySpecStatus write(KiottySpecDecisionKind kind, std::string_view markdown);

    Storage &_storage;
    Meta &_meta;
    ItemStore &_items;
};

}
