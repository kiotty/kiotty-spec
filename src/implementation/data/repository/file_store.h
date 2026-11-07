#pragma once

#include "implementation/data/datasource/project_files.h"
#include "implementation/data/datasource/storage.h"
#include "implementation/data/repository/meta.h"
#include "implementation/data/repository/store_result.h"
#include "implementation/domain/entity/names.h"

#include <string_view>

namespace kiotty::spec::implementation {

class FileStore {
public:
    FileStore(Storage &storage, Meta &meta, const ProjectFiles &projectFiles)
        : _storage(storage), _meta(meta), _projectFiles(projectFiles) {}

    TextResult registerFile(KiottySpecFileRole role, std::string_view path);
    OptionalFileResult find(std::string_view id, int64_t cut = kCurrent);
    FileResult get(std::string_view id, int64_t cut = kCurrent);
    OptionalFileResult byPath(std::string_view path, int64_t cut = kCurrent);
    FlagResult rescan(std::string_view id);
    FileListResult list(KiottySpecFileRole roleOr0, int64_t cut = kCurrent);
    FileListResult changedSince(int64_t cpSeq);

private:
    OptionalFileResult first(std::string_view sql, int64_t cut, Span<const char> key);
    TextResult insert(KiottySpecFileRole role, const PathText &normalized);
    KiottySpecStatus writeVersion(const FileRecord &record);

    Storage &_storage;
    Meta &_meta;
    const ProjectFiles &_projectFiles;
};

}
