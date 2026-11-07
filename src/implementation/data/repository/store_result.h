#pragma once

#include "kiotty_spec.h"
#include "implementation/core/result.h"
#include "implementation/domain/entity/records.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kiotty::spec::implementation {

using TextResult = Result<std::string, KiottySpecStatus>;
using OptionalTextResult = Result<std::optional<std::string>, KiottySpecStatus>;
using TextListResult = Result<std::vector<std::string>, KiottySpecStatus>;
using SequenceResult = Result<int64_t, KiottySpecStatus>;
using IdResult = Result<IdText, KiottySpecStatus>;
using CountResult = Result<size_t, KiottySpecStatus>;
using FlagResult = Result<bool, KiottySpecStatus>;

using ItemResult = Result<ItemRecord, KiottySpecStatus>;
using OptionalItemResult = Result<std::optional<ItemRecord>, KiottySpecStatus>;
using ItemListResult = Result<std::vector<ItemRecord>, KiottySpecStatus>;
using LinkResult = Result<LinkRecord, KiottySpecStatus>;
using LinkListResult = Result<std::vector<LinkRecord>, KiottySpecStatus>;
using FileResult = Result<FileRecord, KiottySpecStatus>;
using OptionalFileResult = Result<std::optional<FileRecord>, KiottySpecStatus>;
using FileListResult = Result<std::vector<FileRecord>, KiottySpecStatus>;
using CheckpointResult = Result<CheckpointRecord, KiottySpecStatus>;
using OptionalCheckpointResult = Result<std::optional<CheckpointRecord>, KiottySpecStatus>;
using CheckpointListResult = Result<std::vector<CheckpointRecord>, KiottySpecStatus>;
using ChangeRequestResult = Result<ChangeRequestRecord, KiottySpecStatus>;
using ChangeRequestListResult = Result<std::vector<ChangeRequestRecord>, KiottySpecStatus>;
using TestRunResult = Result<ResultRecord, KiottySpecStatus>;
using OptionalTestRunResult = Result<std::optional<ResultRecord>, KiottySpecStatus>;
using TestRunListResult = Result<std::vector<ResultRecord>, KiottySpecStatus>;

}
