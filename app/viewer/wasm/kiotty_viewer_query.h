#pragma once

#include "kiotty_spec.h"
#include "kiotty_viewer_json.h"

#include <string>
#include <string_view>
#include <vector>

namespace kiotty::viewer {

struct KindDefinitions {
    std::vector<KiottySpecTagDefinition> tags {};
    std::vector<KiottySpecRelationDefinition> relations {};
    bool is_loaded = false;
};

class ModelDefinitions {
public:
    explicit ModelDefinitions(KiottySpecContext& context);

    std::string formatRelation(KiottySpecItemKind model, uint32_t relation);
    const KindDefinitions& findKind(KiottySpecItemKind kind);

private:
    std::vector<KiottySpecTagDefinition> readTags(KiottySpecItemKind kind);
    std::vector<KiottySpecRelationDefinition> readRelations(KiottySpecItemKind kind);

    KiottySpecContext& _context;
    std::vector<KindDefinitions> _kinds;
};

struct Project {
    std::string name {};
    int64_t created_at = 0;
    bool has_created_at = false;
};

enum class ItemListStyle { PLAIN, WITH_OUT_COUNT };

class QueryWriter {
public:
    QueryWriter(KiottySpecContext& context, const Project& project, std::string& out);

    void writeOverview();
    void writeProgress();
    void writeItems(KiottySpecItemKind kind, ItemListStyle style);
    void writeItem(const KiottySpecItemResult& item);

private:
    void writeProject();
    void writeKindCount(KiottySpecItemKind kind);
    void writePosition();
    void writeUnmetConditions();
    void writeUnmetRow(const KiottySpecUnmetCondition& row);
    void writeCheckpoints();
    void writeCheckpointRow(KiottySpecCheckpointHandle handle, const KiottySpecCheckpoint& row);
    void writeModel(KiottySpecItemKind kind);
    void writeTagDefinition(const KiottySpecTagDefinition& definition);
    void writeRelationDefinition(const KiottySpecRelationDefinition& definition);
    void writeItemList(KiottySpecItemKind kind, ItemListStyle style);
    void writeItemRow(const KiottySpecItem& item, ItemListStyle style);
    void writeItemHead(const KiottySpecItem& item);
    void writeItemTags(KiottySpecItemHandle item, KiottySpecItemKind kind);
    void writeTag(const KiottySpecTag& tag, const KindDefinitions& model);
    void writeTagValues(const KiottySpecTagDefinition& definition, uint64_t value);
    uint64_t countOutRelations(const KiottySpecItem& item, KiottySpecItemHandle self);
    std::vector<KiottySpecRelation> readRelations(const KiottySpecItem& item);
    void writeBody(const KiottySpecItem& item);
    void writeRelations(const KiottySpecItem& item, KiottySpecItemHandle self);
    void writeRelationSide(const KiottySpecRelation& relation, KiottySpecItemHandle other);
    void writeImpact(const KiottySpecItem& item);
    void writeChildren(const KiottySpecItem& item);
    void writeItemReference(std::string_view id);
    void writeCheckpointOrNull(KiottySpecCheckpointHandle handle);
    void writeSuccess();

    KiottySpecContext& _context;
    const Project& _project;
    JsonWriter _json;
    ModelDefinitions _definitions;
};

}
