#pragma once

#include "implementation/data/datasource/storage.h"
#include "implementation/data/repository/meta.h"
#include "implementation/data/repository/store_result.h"
#include "implementation/domain/entity/names.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kiotty::spec::implementation {

struct ItemQuery {
    KiottySpecItemKind kind{};
    KiottySpecItemCategory category{};
    std::optional<std::string> parentId;
    std::optional<std::string> text;
    int64_t cut = kCurrent;
};

struct ItemChange {
    ItemRecord record;
    bool added = false;
};

using ItemChangeListResult = Result<std::vector<ItemChange>, KiottySpecStatus>;

class ItemStore {
public:
    ItemStore(Storage &storage, Meta &meta) : _storage(storage), _meta(meta) {}

    TextResult create(KiottySpecItemKind kind, KiottySpecItemCategory category,
                      const std::optional<std::string> &parentId, std::string_view title, std::string_view body);
    OptionalItemResult find(std::string_view id, int64_t cut = kCurrent);
    ItemResult get(std::string_view id, int64_t cut = kCurrent);
    ItemListResult list(const ItemQuery &query);
    ItemListResult ofKind(KiottySpecItemKind kind, int64_t cut = kCurrent);
    ItemListResult children(std::string_view id, bool recursive, int64_t cut = kCurrent);
    ItemListResult search(std::string_view query, size_t limit);
    ItemChangeListResult changedBetween(int64_t fromSeq, int64_t toSeq);

    KiottySpecStatus update(std::string_view id, const std::optional<std::string> &title,
                            const std::optional<std::string> &body);
    KiottySpecStatus setParent(std::string_view id, const std::optional<std::string> &parentId, int32_t seq);
    KiottySpecStatus remove(std::string_view id);

    static std::string bodyTemplate(KiottySpecItemKind kind, KiottySpecItemCategory category);

private:
    TextResult insert(ItemRecord record, const std::optional<std::string> &parentId);
    SequenceResult nextSiblingSeq(const ItemRecord &record);
    ItemListResult childrenOf(std::string_view parentId, int64_t cut);
    Result<ItemChange, KiottySpecStatus> changeOf(std::string_view id, int64_t fromSeq, int64_t toSeq);
    KiottySpecStatus validateCategory(KiottySpecItemKind kind, KiottySpecItemCategory category);
    KiottySpecStatus validateParent(std::string_view id, const std::optional<std::string> &parentId);
    KiottySpecStatus writeVersion(const ItemRecord &record);

    Storage &_storage;
    Meta &_meta;
};

}
