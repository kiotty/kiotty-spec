#pragma once

#include "implementation/data/repository/meta.h"
#include "implementation/data/datasource/database.h"
#include "implementation/domain/entity/names.h"
#include "implementation/domain/entity/records.h"

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

class ItemStore {
public:
    ItemStore(Database &db, Meta &meta) : db_(db), meta_(meta) {}

    std::string create(KiottySpecItemKind kind, KiottySpecItemCategory category,
                       const std::optional<std::string> &parentId, std::string_view title, std::string_view body);
    std::optional<ItemRecord> find(std::string_view id, int64_t cut = kCurrent);
    ItemRecord get(std::string_view id, int64_t cut = kCurrent);
    std::vector<ItemRecord> list(const ItemQuery &query);
    std::vector<ItemRecord> ofKind(KiottySpecItemKind kind, int64_t cut = kCurrent);
    std::vector<ItemRecord> children(std::string_view id, bool recursive, int64_t cut = kCurrent);
    std::vector<ItemRecord> search(std::string_view query, size_t limit);
    std::vector<ItemChange> changedBetween(int64_t fromSeq, int64_t toSeq);

    void update(std::string_view id, const std::optional<std::string> &title, const std::optional<std::string> &body);
    void setParent(std::string_view id, const std::optional<std::string> &parentId, int32_t seq);
    void remove(std::string_view id);

    static std::string bodyTemplate(KiottySpecItemKind kind, KiottySpecItemCategory category);

private:
    void writeVersion(const ItemRecord &record);
    void validateParent(std::string_view id, const std::optional<std::string> &parentId);

    Database &db_;
    Meta &meta_;
};

}
