#pragma once

#include "implementation/core/records.h"

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace kiotty::core {

// 레코드 하나와, 그 레코드의 문자열을 가리키는 C 뷰를 함께 들고 있는 상자.
// 상자는 unique_ptr 로 고정되어 있어 뷰의 포인터가 무효화되지 않는다.
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

// 레코드 벡터를 커서 원소 목록으로 바꾼다.
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

// 단일 레코드를 malloc 한 덩어리로 묶어 C 호출자에게 넘긴다. KiottySpec_free 로 한 번에 해제된다.
KiottySpecItem *packItem(const ItemRecord &record);
KiottySpecFile *packFile(const FileRecord &record);
KiottySpecCheckpoint *packCheckpoint(const CheckpointRecord &record);
KiottySpecChangeRequest *packChangeRequest(const ChangeRequestRecord &record);
KiottySpecResult *packResult(const ResultRecord &record);
KiottySpecPosition *packPosition(const PositionRecord &record);
char *duplicateString(std::string_view text);

} // namespace kiotty::core

// C 헤더의 불투명 타입 정의.
struct KiottySpecCursor {
    kiotty::core::CursorPayload payload;
};
