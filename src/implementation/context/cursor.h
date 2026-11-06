#pragma once

#include "implementation/domain/entity/records.h"

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace kiotty::spec::implementation {

struct ItemBox { ItemRecord record; std::string cpFrom; KiottySpecItem view{}; };
struct LinkBox { LinkRecord record; std::string cpFrom; KiottySpecLink view{}; };
struct FileBox { FileRecord record; std::string cpFrom; KiottySpecFile view{}; };
struct CheckpointBox { CheckpointRecord record; std::string id; KiottySpecCheckpoint view{}; };
struct ChangeRequestBox { ChangeRequestRecord record; std::string cpOpened; std::string cpClosed; KiottySpecChangeRequest view{}; };
struct ResultBox { ResultRecord record; std::string cpFrom; KiottySpecResult view{}; };
struct MilestoneEntryBox { MilestoneEntryRecord record; std::string implementedCp; KiottySpecMilestoneEntry view{}; };
struct StepRunBox { StepRunRecord record; std::string first, last, confirmed; KiottySpecStepRun view{}; };
struct IdBox { std::string id; };

std::unique_ptr<ItemBox> makeBox(ItemRecord record);
std::unique_ptr<LinkBox> makeBox(LinkRecord record);
std::unique_ptr<FileBox> makeBox(FileRecord record);
std::unique_ptr<CheckpointBox> makeBox(CheckpointRecord record);
std::unique_ptr<ChangeRequestBox> makeBox(ChangeRequestRecord record);
std::unique_ptr<ResultBox> makeBox(ResultRecord record);
std::unique_ptr<MilestoneEntryBox> makeBox(MilestoneEntryRecord record);
std::unique_ptr<StepRunBox> makeBox(StepRunRecord record);
std::unique_ptr<IdBox> makeBox(std::string id);

template <class Box>
using BoxList = std::vector<std::unique_ptr<Box>>;

using CursorPayload = std::variant<BoxList<ItemBox>, BoxList<LinkBox>, BoxList<FileBox>, BoxList<CheckpointBox>,
                                   BoxList<ChangeRequestBox>, BoxList<ResultBox>, BoxList<MilestoneEntryBox>,
                                   BoxList<StepRunBox>, BoxList<IdBox>>;

template <class Record>
auto boxAll(std::vector<Record> records) {
    using Box = typename decltype(makeBox(std::declval<Record>()))::element_type;
    BoxList<Box> boxes;
    boxes.reserve(records.size());
    for (auto &record : records) {
        boxes.push_back(makeBox(std::move(record)));
    }
    return boxes;
}

KiottySpecItem *packItem(const ItemRecord &record);
KiottySpecFile *packFile(const FileRecord &record);
KiottySpecCheckpoint *packCheckpoint(const CheckpointRecord &record);
KiottySpecChangeRequest *packChangeRequest(const ChangeRequestRecord &record);
KiottySpecResult *packResult(const ResultRecord &record);
KiottySpecPosition *packPosition(const PositionRecord &record);
char *duplicateString(std::string_view text);

}

struct KiottySpecCursor {
    kiotty::spec::implementation::CursorPayload payload;
};
