#include "output.h"

#include <iostream>

namespace kiotty::cli {

namespace {

json orNull(const char *text) { return text != nullptr ? json(text) : json(nullptr); }

} // namespace

void check(KiottySpecStatus status) {
    if (status != KiottySpecStatus_Ok) {
        throw CallError(status, KiottySpec_lastError());
    }
}

std::string enumName(const char *type, int value) { return KiottySpec_enumName(type, value); }

int enumValue(const char *type, const std::string &name) {
    int value = 0;
    check(KiottySpec_enumParse(type, name.c_str(), &value));
    return value;
}

json toJson(const KiottySpecItem &item) {
    return {{"id", item.id},
            {"kind", enumName("ItemKind", item.kind)},
            {"category", enumName("ItemCategory", item.category)},
            {"parentId", orNull(item.parentId)},
            {"seq", item.seq},
            {"title", item.title},
            {"body", item.body},
            {"cpFrom", item.cpFrom}};
}

json toJson(const KiottySpecLink &link) {
    return {{"from", link.fromId}, {"type", enumName("LinkType", link.type)}, {"to", link.toId}, {"cpFrom", link.cpFrom}};
}

json toJson(const KiottySpecFile &file) {
    return {{"id", file.id}, {"role", enumName("FileRole", file.role)}, {"path", file.path}, {"hash", file.hash}, {"cpFrom", file.cpFrom}};
}

json toJson(const KiottySpecCheckpoint &checkpoint) {
    return {{"id", checkpoint.id},
            {"step", checkpoint.step},
            {"confirmed", checkpoint.confirmed},
            {"purpose", enumName("CheckpointPurpose", checkpoint.purpose)},
            {"note", checkpoint.note},
            {"createdAt", checkpoint.createdAt}};
}

json toJson(const KiottySpecChangeRequest &change) {
    return {{"id", change.id},
            {"source", enumName("ChangeSource", change.source)},
            {"body", change.body},
            {"entryStep", change.entryStep},
            {"state", enumName("ChangeState", change.state)},
            {"cpOpened", change.cpOpened},
            {"cpClosed", orNull(change.cpClosed)}};
}

json toJson(const KiottySpecResult &result) {
    return {{"target", result.targetId},
            {"result", enumName("TestResult", result.result)},
            {"log", result.log},
            {"cpFrom", result.cpFrom},
            {"runAt", result.runAt}};
}

json toJson(const KiottySpecMilestoneEntry &entry) {
    return {{"milestone", entry.milestoneId},
            {"item", entry.itemId},
            {"state", enumName("ImplState", entry.state)},
            {"implementedCp", orNull(entry.implementedCp)}};
}

json toJson(const KiottySpecStepRun &run) {
    return {{"step", run.step},
            {"first", run.firstCheckpointId},
            {"last", run.lastCheckpointId},
            {"confirmed", orNull(run.confirmedCheckpointId)},
            {"rounds", run.checkpointCount},
            {"reason", run.reason},
            {"lastNote", run.lastNote}};
}

json toJson(const KiottySpecPosition &position) {
    return {{"checkpoint", orNull(position.checkpointId)},
            {"step", position.step},
            {"confirmed", position.confirmed},
            {"released", position.released},
            {"pendingWrites", position.pendingWrites},
            {"nextStep", position.nextStep},
            {"nextAction", position.nextAction}};
}

json drain(KiottySpecCursor *cursor, CursorKind kind) {
    json array = json::array();
    size_t count = 0;
    const KiottySpecStatus status = KiottySpec_Cursor_count(cursor, &count);
    if (status != KiottySpecStatus_Ok) {
        KiottySpec_Cursor_end(cursor);
        check(status);
    }
    try {
        for (size_t i = 0; i < count; ++i) {
            switch (kind) {
            case CursorKind::Item: { const KiottySpecItem *v = nullptr; check(KiottySpec_Cursor_getItem(cursor, i, &v)); array.push_back(toJson(*v)); break; }
            case CursorKind::Link: { const KiottySpecLink *v = nullptr; check(KiottySpec_Cursor_getLink(cursor, i, &v)); array.push_back(toJson(*v)); break; }
            case CursorKind::File: { const KiottySpecFile *v = nullptr; check(KiottySpec_Cursor_getFile(cursor, i, &v)); array.push_back(toJson(*v)); break; }
            case CursorKind::Checkpoint: { const KiottySpecCheckpoint *v = nullptr; check(KiottySpec_Cursor_getCheckpoint(cursor, i, &v)); array.push_back(toJson(*v)); break; }
            case CursorKind::ChangeRequest: { const KiottySpecChangeRequest *v = nullptr; check(KiottySpec_Cursor_getChangeRequest(cursor, i, &v)); array.push_back(toJson(*v)); break; }
            case CursorKind::Result: { const KiottySpecResult *v = nullptr; check(KiottySpec_Cursor_getResult(cursor, i, &v)); array.push_back(toJson(*v)); break; }
            case CursorKind::MilestoneEntry: { const KiottySpecMilestoneEntry *v = nullptr; check(KiottySpec_Cursor_getMilestoneEntry(cursor, i, &v)); array.push_back(toJson(*v)); break; }
            case CursorKind::StepRun: { const KiottySpecStepRun *v = nullptr; check(KiottySpec_Cursor_getStepRun(cursor, i, &v)); array.push_back(toJson(*v)); break; }
            case CursorKind::Id: { const char *v = nullptr; check(KiottySpec_Cursor_getId(cursor, i, &v)); array.push_back(v); break; }
            }
        }
    } catch (...) {
        KiottySpec_Cursor_end(cursor);
        throw;
    }
    KiottySpec_Cursor_end(cursor);
    return array;
}

std::string take(char *text) {
    std::string result = text != nullptr ? text : "";
    KiottySpec_free(text);
    return result;
}

void printJson(const json &value) { std::cout << value.dump(2) << "\n"; }

void printText(const std::string &text) {
    std::cout << text;
    if (!text.empty() && text.back() != '\n') std::cout << "\n";
}

} // namespace kiotty::cli
