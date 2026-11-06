#pragma once

#include "implementation/data/repository/file_store.h"
#include "implementation/data/repository/item_store.h"
#include "implementation/data/datasource/database.h"
#include "implementation/domain/entity/records.h"

#include <string>
#include <string_view>
#include <vector>

namespace kiotty::spec::implementation {

struct ScopeResult {
    KiottySpecScope scope = KIOTTY_SPEC_SCOPE_INTRA_MODULE;
    std::vector<std::string> affectedModules;
};

struct GuiScanResult {
    std::vector<std::string> unknownIds;
    std::vector<ItemRecord> missingViews;
};

class LinkStore {
public:
    LinkStore(Database &db, ItemStore &items, FileStore &files) : db_(db), items_(items), files_(files) {}

    void add(std::string_view fromId, std::string_view toId, KiottySpecLinkType type);
    void remove(std::string_view fromId, std::string_view toId, KiottySpecLinkType type);
    void removeAllFor(std::string_view id);
    bool exists(std::string_view fromId, std::string_view toId, KiottySpecLinkType type, int64_t cut = kCurrent);

    std::vector<LinkRecord> all(int64_t cut = kCurrent);
    std::vector<LinkRecord> list(std::string_view id, KiottySpecDirection dir, KiottySpecLinkType typeOr0,
                                       int64_t cut = kCurrent);
    std::vector<std::string> targets(std::string_view fromId, KiottySpecLinkType type, int64_t cut = kCurrent);
    std::vector<std::string> sources(std::string_view toId, KiottySpecLinkType type, int64_t cut = kCurrent);

    std::vector<std::string> traverse(std::string_view id, KiottySpecDirection dir, int32_t depth);
    std::vector<ItemRecord> impactSince(int64_t cpSeq);
    ScopeResult scopeOf(std::string_view id);
    std::vector<std::string> suspects(std::string_view failedTargetId);
    std::string matrix(KiottySpecItemKind rowKind, KiottySpecItemKind colKind);
    GuiScanResult scanGuiDraft(std::string_view fileId);

private:
    void validate(std::string_view fromId, std::string_view toId, KiottySpecLinkType type);
    void writeVersion(std::string_view fromId, std::string_view toId, KiottySpecLinkType type, bool removed);
    std::vector<std::string> modulesOf(std::string_view id, bool &touchesNonModule);

    Database &db_;
    ItemStore &items_;
    FileStore &files_;
};

}
