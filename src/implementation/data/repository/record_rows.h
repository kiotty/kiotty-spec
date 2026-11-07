#pragma once

#include "implementation/data/datasource/query.h"
#include "implementation/data/datasource/storage.h"
#include "implementation/data/repository/store_result.h"

namespace kiotty::spec::implementation {

ItemResult readItem(Storage& storage, const Row& row);
ItemListResult collectItems(Storage& storage, Query& query);
LinkListResult collectLinks(Storage& storage, Query& query);
FileResult readFileRow(Storage& storage, const Row& row);
FileListResult collectFiles(Storage& storage, Query& query);
CheckpointResult readCheckpoint(Storage& storage, const Row& row);
CheckpointListResult collectCheckpoints(Storage& storage, Query& query);
ChangeRequestResult readChange(Storage& storage, const Row& row);
ChangeRequestListResult collectChanges(Storage& storage, Query& query);
TestRunResult readTestRun(Storage& storage, const Row& row);
TestRunListResult collectTestRuns(Storage& storage, Query& query);

}
