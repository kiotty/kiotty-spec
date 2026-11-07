#pragma once

#include "implementation/data/datasource/storage.h"
#include "implementation/data/repository/item_store.h"
#include "implementation/data/repository/store_result.h"

#include <string_view>
#include <vector>

namespace kiotty::spec::implementation {

struct ItemChange {
    ItemRecord record;
    bool added = false;
};

using ItemChangeResult = Result<ItemChange, KiottySpecStatus>;
using OptionalItemChangeResult = Result<std::optional<ItemChange>, KiottySpecStatus>;
using ItemChangeListResult = Result<std::vector<ItemChange>, KiottySpecStatus>;

class ItemFinder {
public:
    ItemFinder(Storage &storage, ItemStore &items) : _storage(storage), _items(items) {}

    ItemListResult ofKind(KiottySpecItemKind kind, int64_t cut = kCurrent);
    ItemListResult children(std::string_view id, bool recursive, int64_t cut = kCurrent);
    ItemListResult search(std::string_view query, size_t limit);
    ItemChangeListResult changedBetween(int64_t fromSeq, int64_t toSeq);

private:
    ItemListResult childrenOf(std::string_view parentId, int64_t cut);
    OptionalItemChangeResult changeOf(std::string_view id, int64_t fromSeq, int64_t toSeq);

    Storage &_storage;
    ItemStore &_items;
};

}
