#include "output.h"

#include <iostream>

namespace kiotty::cli {

namespace {

json orNull(const char *text) { return text != nullptr ? json(text) : json(nullptr); }

}

void check(KiottySpecStatus status) {
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
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
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
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

std::string escapeCell(const std::string &text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '\t': out += "\\t"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        default: out += c; break;
        }
    }
    return out;
}

template <class Json>
std::string cell(const Json &value) {
    if (value.is_null()) return "";
    if (value.is_string()) return escapeCell(value.template get<std::string>());
    if (value.is_structured()) return escapeCell(value.dump());
    return value.dump();
}

void appendRow(std::string &out, const std::vector<std::string> &fields) {
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) out += '\t';
        out += fields[i];
    }
    out += '\n';
}

template <class Json>
std::string table(const Json &value) {
    if (!value.is_array()) return table(Json::array({value}));
    std::string out;
    if (value.empty()) return out;
    if (!value.front().is_object()) {
        for (const auto &item : value) out += cell(item) + "\n";
        return out;
    }
    std::vector<std::string> keys;
    for (const auto &item : value.front().items()) keys.push_back(item.key());
    appendRow(out, keys);
    for (const auto &row : value) {
        std::vector<std::string> fields;
        for (const auto &key : keys) fields.push_back(row.contains(key) ? cell(row.at(key)) : std::string());
        appendRow(out, fields);
    }
    return out;
}

std::string toTsv(const json &value) { return table(value); }

void printTsv(const json &value) { std::cout << table(value); }

void printOrderedTsv(const nlohmann::ordered_json &value) { std::cout << table(value); }

void printText(const std::string &text) {
    std::cout << text;
    if (!text.empty() && text.back() != '\n') std::cout << "\n";
}

}
