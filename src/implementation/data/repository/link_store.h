#pragma once

#include "implementation/data/datasource/storage.h"
#include "implementation/data/repository/file_store.h"
#include "implementation/data/repository/item_store.h"
#include "implementation/data/repository/store_result.h"

#include <string_view>

namespace kiotty::spec::implementation {

struct LinkEnd {
    bool isFile = false;
    KiottySpecItemKind kind{};
    KiottySpecItemCategory category{};
    KiottySpecFileRole role{};
};

class LinkStore {
public:
    LinkStore(Storage &storage, ItemStore &items, FileStore &files) : _storage(storage), _items(items), _files(files) {}

    KiottySpecStatus add(std::string_view fromId, std::string_view toId, KiottySpecLinkType type);
    KiottySpecStatus remove(std::string_view fromId, std::string_view toId, KiottySpecLinkType type);
    KiottySpecStatus removeAllFor(std::string_view id);
    FlagResult exists(std::string_view fromId, std::string_view toId, KiottySpecLinkType type, int64_t cut = kCurrent);
    LinkListResult list(std::string_view id, KiottySpecDirection dir, KiottySpecLinkType typeOr0, int64_t cut = kCurrent);
    TextListResult targets(std::string_view fromId, KiottySpecLinkType type, int64_t cut = kCurrent);
    TextListResult sources(std::string_view toId, KiottySpecLinkType type, int64_t cut = kCurrent);

private:
    KiottySpecStatus validate(std::string_view fromId, std::string_view toId, KiottySpecLinkType type);
    Result<LinkEnd, KiottySpecStatus> endOf(std::string_view id);
    KiottySpecStatus writeVersion(std::string_view fromId, std::string_view toId, KiottySpecLinkType type, bool removed);

    Storage &_storage;
    ItemStore &_items;
    FileStore &_files;
};

}
