#pragma once

#include "kiotty_session.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kiotty::cli {

int check(const Session& session, KiottySpecStatus status);
const std::string* positional(const Session& session, size_t index);
std::optional<uint32_t> readNumberOption(const Session& session, std::string_view name,
                                         uint32_t fallback);
std::string stepName(uint32_t step);
struct BodyOption {
    std::optional<std::string> text {};
    bool is_unreadable {false};
};
BodyOption bodyOption(const Session& session, const char* text_name, const char* file_name);

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

struct RelationColumn {
    uint32_t index {};
    std::string name {};
};

struct DiffColumns {
    std::vector<TagColumn> tags {};
    std::vector<RelationColumn> relations {};
};

std::optional<ColumnPlan> planColumns(const Session& session, KiottySpecItemKind kind);
std::optional<DiffColumns> planDiffColumns(const Session& session, KiottySpecItemKind kind);
void printItemHeader(const ColumnPlan* plan);
void printItem(const Session& session, const KiottySpecItem& item, const ColumnPlan* plan);
int printItemCursor(const Session& session, const KiottySpecCursorResult& cursor,
                    const ColumnPlan* plan);

int initializeProject(const Session& session);
int addItem(const Session& session);
int renameItem(const Session& session);
int setItem(const Session& session);
int showItem(const Session& session);
int listItems(const Session& session);
int listChildren(const Session& session);
int showBody(const Session& session);
int removeItem(const Session& session);
int showTags(const Session& session);
int setTag(const Session& session);
int addRelation(const Session& session);
int removeRelation(const Session& session);
int listRelations(const Session& session);
int showImpact(const Session& session);
int startCheckpoint(const Session& session);
int resumeCheckpoint(const Session& session);
int completeCheckpoint(const Session& session);
int showCheckpoint(const Session& session);
int listCheckpoints(const Session& session);
int pruneCheckpoints(const Session& session);
int showPosition(const Session& session);
int showHistory(const Session& session);
int showDecision(const Session& session);
int setDecision(const Session& session);
int showModel(const Session& session);
int printDiffSummary(const Session& session, const KiottySpecDiffQuery& query);

}
