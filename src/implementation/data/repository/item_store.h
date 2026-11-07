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

class ItemStore {
public:
    ItemStore(Storage &storage, Meta &meta) : _storage(storage), _meta(meta) {}

    TextResult create(KiottySpecItemKind kind, KiottySpecItemCategory category,
                      const std::optional<std::string> &parentId, std::string_view title, std::string_view body);
    OptionalItemResult find(std::string_view id, int64_t cut = kCurrent);
    ItemResult get(std::string_view id, int64_t cut = kCurrent);
    ItemListResult list(const ItemQuery &query);
    KiottySpecStatus update(std::string_view id, const std::optional<std::string> &title,
                            const std::optional<std::string> &body);
    KiottySpecStatus setParent(std::string_view id, const std::optional<std::string> &parentId, int32_t seq);
    KiottySpecStatus remove(std::string_view id);

private:
    TextResult insert(ItemRecord record, const std::optional<std::string> &parentId);
    SequenceResult nextSiblingSeq(const ItemRecord &record);
    KiottySpecStatus validateCategory(KiottySpecItemKind kind, KiottySpecItemCategory category);
    KiottySpecStatus validateParent(std::string_view id, const std::optional<std::string> &parentId);
    KiottySpecStatus assignTitle(ItemRecord &record, std::string_view title);
    KiottySpecStatus assignParent(ItemRecord &record, const std::optional<std::string> &parentId);
    KiottySpecStatus writeVersion(const ItemRecord &record);

    Storage &_storage;
    Meta &_meta;
};

}
