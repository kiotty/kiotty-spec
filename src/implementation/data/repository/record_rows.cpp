#include "implementation/data/repository/record_rows.h"

#include "implementation/core/text_view.h"
#include "implementation/domain/entity/names.h"

#include <initializer_list>

namespace kiotty::spec::implementation {

namespace {

KiottySpecStatus firstFailure(std::initializer_list<KiottySpecStatus> statuses)
{
    for (const KiottySpecStatus status : statuses) {
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

KiottySpecStatus tooLong(Storage& storage, Span<const char> key)
{
    return storage.reject(KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, {literalText("저장된 값이 상한을 넘음: "), key});
}

LinkResult readLink(Storage& storage, const Row& row)
{
    LinkRecord record;
    const KiottySpecStatus copied = firstFailure({record.fromId.assign(row.text(0)), record.toId.assign(row.text(1))});
    if (copied != KIOTTY_SPEC_STATUS_SUCCESS) { return LinkResult::failure(tooLong(storage, row.text(0))); }
    parse(viewOf(row.text(2)), record.type);
    record.cpSeq = row.integer(3);
    return LinkResult::success(record);
}

}

ItemResult readItem(Storage& storage, const Row& row)
{
    ItemRecord record;
    const KiottySpecStatus copied = firstFailure({record.id.assign(row.text(0)), record.parentId.assign(row.text(5)), record.title.assign(row.text(7))});
    if (copied != KIOTTY_SPEC_STATUS_SUCCESS) { return ItemResult::failure(tooLong(storage, row.text(0))); }
    record.cpSeq = row.integer(1);
    record.wseq = row.integer(2);
    parse(viewOf(row.text(3)), record.kind);
    parse(viewOf(row.text(4)), record.category);
    record.seq = static_cast<int32_t>(row.integer(6));
    record.body = std::string(viewOf(row.text(8)));
    record.removed = row.integer(9) != 0;
    return ItemResult::success(std::move(record));
}

ItemListResult collectItems(Storage& storage, Query& query)
{
    std::vector<ItemRecord> records;
    auto stepped = query.step();
    while (stepped.value) {
        auto record = readItem(storage, query.row());
        if (!record) { return ItemListResult::failure(record.code); }
        records.push_back(std::move(record.value));
        stepped = query.step();
    }
    if (!stepped) { return ItemListResult::failure(storage.check(stepped.code)); }
    return ItemListResult::success(std::move(records));
}

LinkListResult collectLinks(Storage& storage, Query& query)
{
    std::vector<LinkRecord> records;
    auto stepped = query.step();
    while (stepped.value) {
        const auto record = readLink(storage, query.row());
        if (!record) { return LinkListResult::failure(record.code); }
        records.push_back(record.value);
        stepped = query.step();
    }
    if (!stepped) { return LinkListResult::failure(storage.check(stepped.code)); }
    return LinkListResult::success(std::move(records));
}

FileResult readFileRow(Storage& storage, const Row& row)
{
    FileRecord record;
    const KiottySpecStatus copied = firstFailure({record.id.assign(row.text(0)), record.path.assign(row.text(3)), record.hash.assign(row.text(4))});
    if (copied != KIOTTY_SPEC_STATUS_SUCCESS) { return FileResult::failure(tooLong(storage, row.text(0))); }
    record.cpSeq = row.integer(1);
    parse(viewOf(row.text(2)), record.role);
    return FileResult::success(record);
}

FileListResult collectFiles(Storage& storage, Query& query)
{
    std::vector<FileRecord> records;
    auto stepped = query.step();
    while (stepped.value) {
        const auto record = readFileRow(storage, query.row());
        if (!record) { return FileListResult::failure(record.code); }
        records.push_back(record.value);
        stepped = query.step();
    }
    if (!stepped) { return FileListResult::failure(storage.check(stepped.code)); }
    return FileListResult::success(std::move(records));
}

CheckpointResult readCheckpoint(Storage& storage, const Row& row)
{
    CheckpointRecord record;
    if (record.note.assign(row.text(4)) != KIOTTY_SPEC_STATUS_SUCCESS) { return CheckpointResult::failure(tooLong(storage, literalText("checkpoint.note"))); }
    record.seq = row.integer(0);
    record.step = static_cast<int32_t>(row.integer(1));
    record.confirmed = row.integer(2) != 0;
    parse(viewOf(row.text(3)), record.purpose);
    record.createdAt = row.integer(5);
    return CheckpointResult::success(record);
}

CheckpointListResult collectCheckpoints(Storage& storage, Query& query)
{
    std::vector<CheckpointRecord> records;
    auto stepped = query.step();
    while (stepped.value) {
        const auto record = readCheckpoint(storage, query.row());
        if (!record) { return CheckpointListResult::failure(record.code); }
        records.push_back(record.value);
        stepped = query.step();
    }
    if (!stepped) { return CheckpointListResult::failure(storage.check(stepped.code)); }
    return CheckpointListResult::success(std::move(records));
}

ChangeRequestResult readChange(Storage& storage, const Row& row)
{
    ChangeRequestRecord record;
    const KiottySpecStatus copied = firstFailure({record.id.assign(row.text(0)), record.note.assign(row.text(7))});
    if (copied != KIOTTY_SPEC_STATUS_SUCCESS) { return ChangeRequestResult::failure(tooLong(storage, row.text(0))); }
    parse(viewOf(row.text(1)), record.source);
    record.body = std::string(viewOf(row.text(2)));
    record.entryStep = static_cast<int32_t>(row.integer(3));
    parse(viewOf(row.text(4)), record.state);
    record.cpOpened = row.integer(5);
    if (!row.isNull(6)) { record.cpClosed = row.integer(6); }
    return ChangeRequestResult::success(std::move(record));
}

ChangeRequestListResult collectChanges(Storage& storage, Query& query)
{
    std::vector<ChangeRequestRecord> records;
    auto stepped = query.step();
    while (stepped.value) {
        auto record = readChange(storage, query.row());
        if (!record) { return ChangeRequestListResult::failure(record.code); }
        records.push_back(std::move(record.value));
        stepped = query.step();
    }
    if (!stepped) { return ChangeRequestListResult::failure(storage.check(stepped.code)); }
    return ChangeRequestListResult::success(std::move(records));
}

TestRunResult readTestRun(Storage& storage, const Row& row)
{
    ResultRecord record;
    if (record.targetId.assign(row.text(0)) != KIOTTY_SPEC_STATUS_SUCCESS) { return TestRunResult::failure(tooLong(storage, row.text(0))); }
    parse(viewOf(row.text(1)), record.result);
    record.log = std::string(viewOf(row.text(2)));
    record.cpSeq = row.integer(3);
    record.runAt = row.integer(4);
    return TestRunResult::success(std::move(record));
}

TestRunListResult collectTestRuns(Storage& storage, Query& query)
{
    std::vector<ResultRecord> records;
    auto stepped = query.step();
    while (stepped.value) {
        auto record = readTestRun(storage, query.row());
        if (!record) { return TestRunListResult::failure(record.code); }
        records.push_back(std::move(record.value));
        stepped = query.step();
    }
    if (!stepped) { return TestRunListResult::failure(storage.check(stepped.code)); }
    return TestRunListResult::success(std::move(records));
}

}
