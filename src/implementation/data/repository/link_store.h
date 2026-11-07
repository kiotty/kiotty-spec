#pragma once

#include "implementation/data/datasource/storage.h"
#include "implementation/data/repository/file_store.h"
#include "implementation/data/repository/item_store.h"
#include "implementation/data/repository/store_result.h"

#include <set>
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

struct LinkEnd {
    bool isFile = false;
    KiottySpecItemKind kind{};
    KiottySpecItemCategory category{};
    KiottySpecFileRole role{};
};

struct ModuleTouch {
    std::vector<std::string> modules;
    bool touchesNonModule = false;
};

using ScopeResultResult = Result<ScopeResult, KiottySpecStatus>;
using GuiScanResultResult = Result<GuiScanResult, KiottySpecStatus>;

class LinkStore {
public:
    LinkStore(Storage &storage, ItemStore &items, FileStore &files) : _storage(storage), _items(items), _files(files) {}

    KiottySpecStatus add(std::string_view fromId, std::string_view toId, KiottySpecLinkType type);
    KiottySpecStatus remove(std::string_view fromId, std::string_view toId, KiottySpecLinkType type);
    KiottySpecStatus removeAllFor(std::string_view id);
    FlagResult exists(std::string_view fromId, std::string_view toId, KiottySpecLinkType type, int64_t cut = kCurrent);

    LinkListResult all(int64_t cut = kCurrent);
    LinkListResult list(std::string_view id, KiottySpecDirection dir, KiottySpecLinkType typeOr0, int64_t cut = kCurrent);
    TextListResult targets(std::string_view fromId, KiottySpecLinkType type, int64_t cut = kCurrent);
    TextListResult sources(std::string_view toId, KiottySpecLinkType type, int64_t cut = kCurrent);

    TextListResult traverse(std::string_view id, KiottySpecDirection dir, int32_t depth);
    ItemListResult impactSince(int64_t cpSeq);
    ScopeResultResult scopeOf(std::string_view id);
    TextListResult suspects(std::string_view failedTargetId);
    TextResult matrix(KiottySpecItemKind rowKind, KiottySpecItemKind colKind);
    GuiScanResultResult scanGuiDraft(std::string_view fileId);

private:
    KiottySpecStatus validate(std::string_view fromId, std::string_view toId, KiottySpecLinkType type);
    Result<LinkEnd, KiottySpecStatus> endOf(std::string_view id);
    KiottySpecStatus writeVersion(std::string_view fromId, std::string_view toId, KiottySpecLinkType type, bool removed);
    TextListResult neighbors(std::string_view id, KiottySpecDirection dir);
    Result<std::set<std::string>, KiottySpecStatus> changedIds(int64_t cpSeq);
    Result<std::set<std::string>, KiottySpecStatus> linkedIds(const std::set<std::string> &changed, int64_t cpSeq);
    Result<ModuleTouch, KiottySpecStatus> modulesOf(std::string_view id);
    KiottySpecStatus addDesign(ModuleTouch &touch, std::string_view designId);
    Result<std::set<std::string>, KiottySpecStatus> dependents(const std::vector<std::string> &modules);
    TextListResult ruleSuspects();
    KiottySpecStatus suspectsOf(std::string_view verifiedId, std::vector<std::string> &designs, std::vector<std::string> &result);

    Storage &_storage;
    ItemStore &_items;
    FileStore &_files;
};

}
