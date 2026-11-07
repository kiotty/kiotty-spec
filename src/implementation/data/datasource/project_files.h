#pragma once

#include "implementation/core/failure.h"
#include "implementation/data/datasource/file_system.h"
#include "implementation/domain/entity/records.h"

#include <optional>
#include <string>

namespace kiotty::spec::implementation {

using PathResult = Result<PathText, KiottySpecStatus>;

class ProjectFiles
{
public:
    ProjectFiles(const SystemPath& root, Failure& failure) : _root(root), _failure(failure) {}

    PathResult normalize(Span<const char> path) const;
    HashText hashOf(Span<const char> normalizedPath) const;
    std::optional<std::string> read(Span<const char> normalizedPath) const;

private:
    SystemPathResult locate(Span<const char> normalizedPath) const;

    const SystemPath& _root;
    Failure& _failure;
};

}
