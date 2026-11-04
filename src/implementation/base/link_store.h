#pragma once

#include "implementation/base/file_store.h"
#include "implementation/base/item_store.h"
#include "implementation/core/database.h"
#include "implementation/core/records.h"

#include <string>
#include <string_view>
#include <vector>

namespace kiotty::base {

struct ScopeResult {
    KiottySpecScope scope = KiottySpecScope_IntraModule;
    std::vector<std::string> affectedModules;
};

struct GuiScanResult {
    std::vector<std::string> unknownIds;           // html 에는 있으나 DB 에 없는 ID
    std::vector<core::ItemRecord> missingViews;    // 이 파일이 구현한다고 링크됐으나 html 에 없는 뷰
};

// 링크: "from 은 to 를 ~한다". 항목과 파일 사이의 추적 관계.
class LinkStore {
public:
    LinkStore(core::Database &db, ItemStore &items, FileStore &files) : db_(db), items_(items), files_(files) {}

    void add(std::string_view fromId, std::string_view toId, KiottySpecLinkType type);
    void remove(std::string_view fromId, std::string_view toId, KiottySpecLinkType type);
    void removeAllFor(std::string_view id);
    bool exists(std::string_view fromId, std::string_view toId, KiottySpecLinkType type, int64_t cut = core::kCurrent);

    std::vector<core::LinkRecord> all(int64_t cut = core::kCurrent);
    std::vector<core::LinkRecord> list(std::string_view id, KiottySpecDirection dir, KiottySpecLinkType typeOr0,
                                       int64_t cut = core::kCurrent);
    std::vector<std::string> targets(std::string_view fromId, KiottySpecLinkType type, int64_t cut = core::kCurrent);
    std::vector<std::string> sources(std::string_view toId, KiottySpecLinkType type, int64_t cut = core::kCurrent);

    std::vector<std::string> traverse(std::string_view id, KiottySpecDirection dir, int32_t depth);
    std::vector<core::ItemRecord> impactSince(int64_t cpSeq);
    ScopeResult scopeOf(std::string_view id);
    std::vector<std::string> suspects(std::string_view failedTargetId);
    std::string matrix(KiottySpecItemKind rowKind, KiottySpecItemKind colKind);
    GuiScanResult scanGuiDraft(std::string_view fileId);

private:
    void validate(std::string_view fromId, std::string_view toId, KiottySpecLinkType type);
    void writeVersion(std::string_view fromId, std::string_view toId, KiottySpecLinkType type, bool removed);
    std::vector<std::string> modulesOf(std::string_view id, bool &touchesNonModule);

    core::Database &db_;
    ItemStore &items_;
    FileStore &files_;
};

} // namespace kiotty::base
