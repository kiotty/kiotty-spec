#pragma once

#include "kiotty_session.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kiotty::cli {

int check(Session& session, KiottySpecStatus status);
const std::string* positional(Session& session, size_t index);
std::string stepName(uint32_t step);
struct BodyOption {
    std::optional<std::string> text {};
    bool is_unreadable {false};
};
BodyOption bodyOption(Session& session, const char* text_name, const char* file_name);

struct Column {
    std::string header {};
    bool is_tag {false};
    TagColumn tag {};
    std::map<std::string, std::string> targets {};
};

struct ColumnPlan {
    std::vector<Column> columns {};
    std::vector<KiottySpecTagQuery> queries {};
};

std::optional<ColumnPlan> planColumns(Session& session, KiottySpecItemKind kind);
void printItemHeader(const ColumnPlan* plan);
void printItem(Session& session, const KiottySpecItem& item, const ColumnPlan* plan);
int printItemCursor(Session& session, const KiottySpecCursorResult& cursor, const ColumnPlan* plan);

int initProject(Session& session);
int addItem(Session& session);
int renameItem(Session& session);
int setItem(Session& session);
int showItem(Session& session);
int listItems(Session& session);
int listChildren(Session& session);
int showBody(Session& session);
int removeItem(Session& session);
int showTags(Session& session);
int setTag(Session& session);
int addRelation(Session& session);
int removeRelation(Session& session);
int listRelations(Session& session);
int showImpact(Session& session);
int startCheckpoint(Session& session);
int resumeCheckpoint(Session& session);
int completeCheckpoint(Session& session);
int showCheckpoint(Session& session);
int listCheckpoints(Session& session);
int pruneCheckpoints(Session& session);
int showPosition(Session& session);
int showHistory(Session& session);
int showDecision(Session& session);
int setDecision(Session& session);
int showModel(Session& session);

}
