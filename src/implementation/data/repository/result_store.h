#pragma once

#include "implementation/data/repository/item_store.h"
#include "implementation/data/datasource/database.h"
#include "implementation/domain/entity/records.h"

#include <optional>
#include <string_view>
#include <vector>

namespace kiotty::spec::implementation {

struct ResultSummary {
    size_t total = 0;
    size_t pass = 0;
    size_t fail = 0;
    size_t notRun = 0;
};

class ResultStore {
public:
    ResultStore(Database &db, ItemStore &items) : db_(db), items_(items) {}

    void record(std::string_view targetId, KiottySpecTestResult result, std::string_view log);
    std::optional<ResultRecord> last(std::string_view targetId);
    std::vector<ResultRecord> history(std::string_view targetId);
    std::vector<ItemRecord> failed(KiottySpecItemKind kindOr0);
    ResultSummary summary(KiottySpecItemKind kind);

private:
    Database &db_;
    ItemStore &items_;
};

}
