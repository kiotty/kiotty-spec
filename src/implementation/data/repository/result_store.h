#pragma once

#include "implementation/data/datasource/storage.h"
#include "implementation/data/repository/item_finder.h"
#include "implementation/data/repository/item_store.h"
#include "implementation/data/repository/store_result.h"

#include <string_view>

namespace kiotty::spec::implementation {

struct ResultSummary {
    size_t total = 0;
    size_t pass = 0;
    size_t fail = 0;
    size_t notRun = 0;
};

using ResultSummaryResult = Result<ResultSummary, KiottySpecStatus>;

class ResultStore {
public:
    ResultStore(Storage &storage, ItemStore &items, ItemFinder &finder) : _storage(storage), _items(items), _finder(finder) {}

    KiottySpecStatus record(std::string_view targetId, KiottySpecTestResult result, std::string_view log);
    OptionalTestRunResult last(std::string_view targetId);
    TestRunListResult history(std::string_view targetId);
    ItemListResult failed(KiottySpecItemKind kindOr0);
    ResultSummaryResult summary(KiottySpecItemKind kind);

private:
    KiottySpecStatus insert(std::string_view targetId, KiottySpecTestResult result, std::string_view log);
    ItemListResult failedOfKind(KiottySpecItemKind kind);

    Storage &_storage;
    ItemStore &_items;
    ItemFinder &_finder;
};

}
