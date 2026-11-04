#include "implementation/core/cursor.h"

#include "implementation/core/names.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <new>

namespace kiotty::core {

namespace {

const char *orNull(const std::string &text) { return text.empty() ? nullptr : text.c_str(); }

// 뷰를 복사한 뒤, 지정한 const char* 멤버들이 가리키는 문자열을 같은 메모리 덩어리 뒤쪽에 복사한다.
template <class View, size_t N>
View *pack(const View &view, const std::array<const char *View::*, N> &members) {
    size_t total = sizeof(View);
    for (const auto member : members) {
        if (view.*member != nullptr) {
            total += std::strlen(view.*member) + 1;
        }
    }
    auto *block = static_cast<char *>(std::malloc(total));
    if (block == nullptr) {
        throw std::bad_alloc();
    }
    auto *out = new (block) View(view);
    char *tail = block + sizeof(View);
    for (const auto member : members) {
        if (view.*member != nullptr) {
            const size_t size = std::strlen(view.*member) + 1;
            std::memcpy(tail, view.*member, size);
            out->*member = tail;
            tail += size;
        }
    }
    return out;
}

} // namespace

std::unique_ptr<ItemBox> makeBox(ItemRecord record) {
    auto box = std::make_unique<ItemBox>();
    box->record = std::move(record);
    box->cpFrom = checkpointName(box->record.cpSeq);
    const auto &r = box->record;
    box->view = KiottySpecItem{r.id.c_str(), r.kind, r.category, orNull(r.parentId), r.seq,
                               r.title.c_str(), r.body.c_str(), box->cpFrom.c_str()};
    return box;
}

std::unique_ptr<LinkBox> makeBox(LinkRecord record) {
    auto box = std::make_unique<LinkBox>();
    box->record = std::move(record);
    box->cpFrom = checkpointName(box->record.cpSeq);
    const auto &r = box->record;
    box->view = KiottySpecLink{r.fromId.c_str(), r.toId.c_str(), r.type, box->cpFrom.c_str()};
    return box;
}

std::unique_ptr<FileBox> makeBox(FileRecord record) {
    auto box = std::make_unique<FileBox>();
    box->record = std::move(record);
    box->cpFrom = checkpointName(box->record.cpSeq);
    const auto &r = box->record;
    box->view = KiottySpecFile{r.id.c_str(), r.role, r.path.c_str(), r.hash.c_str(), box->cpFrom.c_str()};
    return box;
}

std::unique_ptr<CheckpointBox> makeBox(CheckpointRecord record) {
    auto box = std::make_unique<CheckpointBox>();
    box->record = std::move(record);
    box->id = checkpointName(box->record.seq);
    const auto &r = box->record;
    box->view = KiottySpecCheckpoint{box->id.c_str(), r.step, r.confirmed, r.purpose, r.note.c_str(), r.createdAt};
    return box;
}

std::unique_ptr<ChangeRequestBox> makeBox(ChangeRequestRecord record) {
    auto box = std::make_unique<ChangeRequestBox>();
    box->record = std::move(record);
    box->cpOpened = checkpointName(box->record.cpOpened);
    box->cpClosed = box->record.cpClosed ? checkpointName(*box->record.cpClosed) : std::string();
    const auto &r = box->record;
    box->view = KiottySpecChangeRequest{r.id.c_str(), r.source, r.body.c_str(), r.entryStep, r.state,
                                        box->cpOpened.c_str(), orNull(box->cpClosed)};
    return box;
}

std::unique_ptr<ResultBox> makeBox(ResultRecord record) {
    auto box = std::make_unique<ResultBox>();
    box->record = std::move(record);
    box->cpFrom = checkpointName(box->record.cpSeq);
    const auto &r = box->record;
    box->view = KiottySpecResult{r.targetId.c_str(), r.result, r.log.c_str(), box->cpFrom.c_str(), r.runAt};
    return box;
}

std::unique_ptr<MilestoneEntryBox> makeBox(MilestoneEntryRecord record) {
    auto box = std::make_unique<MilestoneEntryBox>();
    box->record = std::move(record);
    box->implementedCp = box->record.implementedCpSeq ? checkpointName(*box->record.implementedCpSeq) : std::string();
    const auto &r = box->record;
    box->view = KiottySpecMilestoneEntry{r.milestoneId.c_str(), r.itemId.c_str(), r.state, orNull(box->implementedCp)};
    return box;
}

std::unique_ptr<StepRunBox> makeBox(StepRunRecord record) {
    auto box = std::make_unique<StepRunBox>();
    box->record = std::move(record);
    box->first = checkpointName(box->record.firstSeq);
    box->last = checkpointName(box->record.lastSeq);
    box->confirmed = box->record.confirmedSeq ? checkpointName(*box->record.confirmedSeq) : std::string();
    const auto &r = box->record;
    box->view = KiottySpecStepRun{r.step, box->first.c_str(), box->last.c_str(), orNull(box->confirmed),
                                  r.count, r.lastNote.c_str(), r.reason.c_str()};
    return box;
}

KiottySpecPosition *packPosition(const PositionRecord &record) {
    const std::string cp = record.checkpointSeq ? checkpointName(*record.checkpointSeq) : std::string();
    const KiottySpecPosition view{orNull(cp), record.step, record.confirmed, record.released,
                                  record.pendingWrites, record.nextStep, record.nextAction.c_str()};
    return pack(view, std::array<const char *KiottySpecPosition::*, 2>{&KiottySpecPosition::checkpointId,
                                                                        &KiottySpecPosition::nextAction});
}

std::unique_ptr<IdBox> makeBox(std::string id) {
    auto box = std::make_unique<IdBox>();
    box->id = std::move(id);
    return box;
}

KiottySpecItem *packItem(const ItemRecord &record) {
    const auto box = makeBox(record);
    return pack(box->view, std::array<const char *KiottySpecItem::*, 5>{
                               &KiottySpecItem::id, &KiottySpecItem::parentId, &KiottySpecItem::title,
                               &KiottySpecItem::body, &KiottySpecItem::cpFrom});
}

KiottySpecFile *packFile(const FileRecord &record) {
    const auto box = makeBox(record);
    return pack(box->view, std::array<const char *KiottySpecFile::*, 4>{
                               &KiottySpecFile::id, &KiottySpecFile::path, &KiottySpecFile::hash,
                               &KiottySpecFile::cpFrom});
}

KiottySpecCheckpoint *packCheckpoint(const CheckpointRecord &record) {
    const auto box = makeBox(record);
    return pack(box->view, std::array<const char *KiottySpecCheckpoint::*, 2>{
                               &KiottySpecCheckpoint::id, &KiottySpecCheckpoint::note});
}

KiottySpecChangeRequest *packChangeRequest(const ChangeRequestRecord &record) {
    const auto box = makeBox(record);
    return pack(box->view, std::array<const char *KiottySpecChangeRequest::*, 4>{
                               &KiottySpecChangeRequest::id, &KiottySpecChangeRequest::body,
                               &KiottySpecChangeRequest::cpOpened, &KiottySpecChangeRequest::cpClosed});
}

KiottySpecResult *packResult(const ResultRecord &record) {
    const auto box = makeBox(record);
    return pack(box->view, std::array<const char *KiottySpecResult::*, 3>{
                               &KiottySpecResult::targetId, &KiottySpecResult::log, &KiottySpecResult::cpFrom});
}

char *duplicateString(std::string_view text) {
    auto *out = static_cast<char *>(std::malloc(text.size() + 1));
    if (out == nullptr) {
        throw std::bad_alloc();
    }
    std::memcpy(out, text.data(), text.size());
    out[text.size()] = '\0';
    return out;
}

} // namespace kiotty::core
