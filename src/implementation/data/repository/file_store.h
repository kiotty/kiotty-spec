#pragma once

#include "implementation/data/datasource/storage.h"
#include "implementation/data/repository/meta.h"
#include "implementation/data/repository/store_result.h"
#include "implementation/domain/entity/names.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace kiotty::spec::implementation {

class FileStore {
public:
    FileStore(Storage &storage, Meta &meta, std::filesystem::path root)
        : _storage(storage), _meta(meta), _root(std::move(root)) {}

    TextResult registerFile(KiottySpecFileRole role, std::string_view path);
    OptionalFileResult find(std::string_view id, int64_t cut = kCurrent);
    FileResult get(std::string_view id, int64_t cut = kCurrent);
    OptionalFileResult byPath(std::string_view path, int64_t cut = kCurrent);
    FlagResult rescan(std::string_view id);
    FileListResult list(KiottySpecFileRole roleOr0, int64_t cut = kCurrent);
    FileListResult changedSince(int64_t cpSeq);

    TextResult normalize(std::string_view path) const;
    std::filesystem::path absolute(const FileRecord &file) const;
    std::string hashOf(std::string_view normalizedPath) const;
    std::optional<std::string> read(const FileRecord &file) const;

private:
    OptionalFileResult first(std::string_view sql, int64_t cut, std::string_view key);
    TextResult insert(KiottySpecFileRole role, const std::string &normalized);
    KiottySpecStatus writeVersion(const FileRecord &record);

    Storage &_storage;
    Meta &_meta;
    std::filesystem::path _root;
};

}
