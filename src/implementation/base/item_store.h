#pragma once

#include "implementation/base/meta.h"
#include "implementation/core/database.h"
#include "implementation/core/names.h"
#include "implementation/core/records.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kiotty::base {

struct ItemQuery {
    KiottySpecItemKind kind{};                 // 0 이면 전체
    KiottySpecItemCategory category{};         // 0 이면 전체
    std::optional<std::string> parentId;
    std::optional<std::string> text;
    int64_t cut = core::kCurrent;
};

struct ItemChange {
    core::ItemRecord record;  // 구간 끝 시점의 버전 (removed 일 수 있음)
    bool added = false;       // 구간 시작 시점에는 없던 항목
};

// 항목: 체크포인트마다 버전이 쌓이고, 현재 라운드의 쓰기는 PENDING 버전 하나에 덮어쓴다.
class ItemStore {
public:
    ItemStore(core::Database &db, Meta &meta) : db_(db), meta_(meta) {}

    std::string create(KiottySpecItemKind kind, KiottySpecItemCategory category,
                       const std::optional<std::string> &parentId, std::string_view title, std::string_view body);
    std::optional<core::ItemRecord> find(std::string_view id, int64_t cut = core::kCurrent);
    core::ItemRecord get(std::string_view id, int64_t cut = core::kCurrent);
    std::vector<core::ItemRecord> list(const ItemQuery &query);
    std::vector<core::ItemRecord> ofKind(KiottySpecItemKind kind, int64_t cut = core::kCurrent);
    std::vector<core::ItemRecord> children(std::string_view id, bool recursive, int64_t cut = core::kCurrent);
    std::vector<core::ItemRecord> search(std::string_view query, size_t limit);
    std::vector<ItemChange> changedBetween(int64_t fromSeq, int64_t toSeq);

    void update(std::string_view id, const std::optional<std::string> &title, const std::optional<std::string> &body);
    void setParent(std::string_view id, const std::optional<std::string> &parentId, int32_t seq);
    void remove(std::string_view id);

    static std::string bodyTemplate(KiottySpecItemKind kind, KiottySpecItemCategory category);

private:
    void writeVersion(const core::ItemRecord &record);
    void validateParent(std::string_view id, const std::optional<std::string> &parentId);

    core::Database &db_;
    Meta &meta_;
};

} // namespace kiotty::base
