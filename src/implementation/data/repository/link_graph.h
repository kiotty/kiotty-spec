#pragma once

#include "implementation/data/datasource/project_files.h"
#include "implementation/data/datasource/storage.h"
#include "implementation/data/repository/file_store.h"
#include "implementation/data/repository/item_finder.h"
#include "implementation/data/repository/item_store.h"
#include "implementation/data/repository/link_store.h"
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

struct ModuleTouch {
    std::vector<std::string> modules;
    bool touchesNonModule = false;
};

using ScopeResultResult = Result<ScopeResult, KiottySpecStatus>;
using GuiScanResultResult = Result<GuiScanResult, KiottySpecStatus>;

class LinkGraph {
public:
    LinkGraph(Storage &storage, LinkStore &links, ItemStore &items, ItemFinder &finder, FileStore &files,
              const ProjectFiles &projectFiles)
        : _storage(storage), _links(links), _items(items), _finder(finder), _files(files), _projectFiles(projectFiles) {}

    LinkListResult all(int64_t cut = kCurrent);
    TextListResult traverse(std::string_view id, KiottySpecDirection dir, int32_t depth);
    ItemListResult impactSince(int64_t cpSeq);
    ScopeResultResult scopeOf(std::string_view id);
    TextListResult suspects(std::string_view failedTargetId);
    TextResult matrix(KiottySpecItemKind rowKind, KiottySpecItemKind colKind);
    GuiScanResultResult scanGuiDraft(std::string_view fileId);

private:
    TextListResult neighbors(std::string_view id, KiottySpecDirection dir);
    Result<std::set<std::string>, KiottySpecStatus> changedIds(int64_t cpSeq);
    Result<std::set<std::string>, KiottySpecStatus> linkedIds(const std::set<std::string> &changed, int64_t cpSeq);
    Result<ModuleTouch, KiottySpecStatus> modulesOf(std::string_view id);
    KiottySpecStatus addDesign(ModuleTouch &touch, std::string_view designId);
    Result<std::set<std::string>, KiottySpecStatus> dependents(const std::vector<std::string> &modules);
    TextListResult ruleSuspects();
    KiottySpecStatus suspectsOf(std::string_view verifiedId, std::vector<std::string> &designs, std::vector<std::string> &result);

    Storage &_storage;
    LinkStore &_links;
    ItemStore &_items;
    ItemFinder &_finder;
    FileStore &_files;
    const ProjectFiles &_projectFiles;
};

}
