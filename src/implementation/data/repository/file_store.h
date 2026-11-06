#pragma once

#include "implementation/data/repository/meta.h"
#include "implementation/data/datasource/database.h"
#include "implementation/domain/entity/names.h"
#include "implementation/domain/entity/records.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kiotty::spec::implementation {

class FileStore {
public:
    FileStore(Database &db, Meta &meta, std::filesystem::path root)
        : db_(db), meta_(meta), root_(std::move(root)) {}

    std::string registerFile(KiottySpecFileRole role, std::string_view path);
    std::optional<FileRecord> find(std::string_view id, int64_t cut = kCurrent);
    FileRecord get(std::string_view id, int64_t cut = kCurrent);
    std::optional<FileRecord> byPath(std::string_view path, int64_t cut = kCurrent);
    bool rescan(std::string_view id);
    std::vector<FileRecord> list(KiottySpecFileRole roleOr0, int64_t cut = kCurrent);
    std::vector<FileRecord> changedSince(int64_t cpSeq);

    std::string normalize(std::string_view path) const;
    std::filesystem::path absolute(const FileRecord &file) const;
    std::string hashOf(std::string_view normalizedPath) const;
    std::optional<std::string> read(const FileRecord &file) const;

private:
    void writeVersion(const FileRecord &record);

    Database &db_;
    Meta &meta_;
    std::filesystem::path root_;
};

}
