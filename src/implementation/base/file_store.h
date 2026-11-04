#pragma once

#include "implementation/base/meta.h"
#include "implementation/core/database.h"
#include "implementation/core/names.h"
#include "implementation/core/records.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kiotty::base {

// 파일: GUI 초안, C 헤더, 소스. 경로는 프로젝트 루트 기준, 내용은 해시로 추적한다.
class FileStore {
public:
    FileStore(core::Database &db, Meta &meta, std::filesystem::path root)
        : db_(db), meta_(meta), root_(std::move(root)) {}

    std::string registerFile(KiottySpecFileRole role, std::string_view path);
    std::optional<core::FileRecord> find(std::string_view id, int64_t cut = core::kCurrent);
    core::FileRecord get(std::string_view id, int64_t cut = core::kCurrent);
    std::optional<core::FileRecord> byPath(std::string_view path, int64_t cut = core::kCurrent);
    bool rescan(std::string_view id);
    std::vector<core::FileRecord> list(KiottySpecFileRole roleOr0, int64_t cut = core::kCurrent);
    std::vector<core::FileRecord> changedSince(int64_t cpSeq);

    std::string normalize(std::string_view path) const;   // 루트 기준 상대 경로 (슬래시)
    std::filesystem::path absolute(const core::FileRecord &file) const;
    std::string hashOf(std::string_view normalizedPath) const;
    std::optional<std::string> read(const core::FileRecord &file) const;

private:
    void writeVersion(const core::FileRecord &record);

    core::Database &db_;
    Meta &meta_;
    std::filesystem::path root_;
};

} // namespace kiotty::base
