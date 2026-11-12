#include "kiotty_commands.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace kiotty::cli {

namespace {

constexpr uint32_t DEFAULT_HISTORY_DEPTH = 3;
const std::vector<std::string> CHECKPOINT_HEADER = { "checkpoint",  "step",       "state",
                                                     "description", "created_at", "previous" };

void printCheckpoint(KiottySpecCheckpointHandle handle, const KiottySpecCheckpoint& checkpoint);
int showKindModel(const Session& session, const std::string& name);
void printTagDefinition(const Session& session, KiottySpecItemKind kind, uint32_t index);
void printRelationDefinition(const Session& session, KiottySpecItemKind kind, uint32_t index);
std::string describeCardinality(KiottySpecCardinality cardinality);

}

int startCheckpoint(const Session& session)
{
    const std::string* step = positional(session, 0);
    if (step == nullptr) { return usage(findMessage(MessageKey::CHECKPOINT_START_USAGE)); }
    const unsigned long value = std::strtoul(step->c_str(), nullptr, DECIMAL_BASE);
    const std::string description = session.args.option("description").value_or("");
    const KiottySpecCheckpointHandleResult started = KiottySpec_startCheckpoint(
        session.context, static_cast<KiottySpecStep>(value), textOf(description));
    if (started.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, started.code); }
    std::printf("%s\n", checkpointName(started.value).c_str());
    return 0;
}

int resumeCheckpoint(const Session& session)
{ return check(session, KiottySpec_resumeCheckpoint(session.context).code); }

int completeCheckpoint(const Session& session)
{ return check(session, KiottySpec_completeCheckpoint(session.context, session.actor).code); }

int showCheckpoint(const Session& session)
{
    KiottySpecCheckpointHandle handle = KIOTTY_SPEC_CHECKPOINT_CURRENT;
    const std::string* name = positional(session, 0);
    if (name) {
        const std::optional<KiottySpecCheckpointHandle> parsed = parseCheckpoint(*name);
        if (!parsed) { return usage(findMessage(MessageKey::CHECKPOINT_NAME_USAGE)); }
        handle = *parsed;
    }
    const KiottySpecCheckpointResult checkpoint =
        KiottySpec_readCheckpoint(session.context, handle);
    if (checkpoint.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, checkpoint.code); }
    printRow(CHECKPOINT_HEADER);
    printCheckpoint(checkpoint.handle, checkpoint.value);
    return 0;
}

int listCheckpoints(const Session& session)
{
    uint32_t step = 0;
    const std::optional<std::string> text = session.args.option("step");
    if (text) {
        step = static_cast<uint32_t>(std::strtoul(text->c_str(), nullptr, DECIMAL_BASE));
    }
    const KiottySpecCursorResult cursor =
        KiottySpec_openCheckpointCursor(session.context, static_cast<KiottySpecStep>(step));
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, cursor.code); }
    printRow(CHECKPOINT_HEADER);
    KiottySpecCheckpointPage page {};
    while (true) {
        const KiottySpecStatusResult fetched =
            KiottySpec_fetchCheckpointPage(session.context, cursor.value, &page);
        if (fetched.code != KIOTTY_SPEC_STATUS_SUCCESS || page.length == 0) { break; }
        for (size_t index = 0; index < page.length; ++index) {
            printCheckpoint(page.handles[index], page.elements[index]);
        }
    }
    KiottySpec_closeCursor(session.context, cursor.value);
    return 0;
}

int pruneCheckpoints(const Session& session)
{
    const std::string* name = positional(session, 0);
    const std::optional<KiottySpecCheckpointHandle> base =
        name == nullptr ? std::nullopt : parseCheckpoint(*name);
    if (!base) { return usage(findMessage(MessageKey::CHECKPOINT_PRUNE_USAGE)); }
    const KiottySpecSizeResult pruned =
        KiottySpec_pruneCheckpoints(session.context, session.actor, *base);
    if (pruned.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, pruned.code); }
    printRow({"removed"});
    printRow({std::to_string(pruned.value)});
    return 0;
}

int showPosition(const Session& session)
{
    const KiottySpecPositionResult position = KiottySpec_readPosition(session.context);
    if (position.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, position.code); }
    printRow({"step"});
    printRow({stepName(position.value.step)});
    return 0;
}

int showHistory(const Session& session)
{
    uint32_t depth = DEFAULT_HISTORY_DEPTH;
    const std::optional<std::string> text = session.args.option("depth");
    if (text) {
        char* end = nullptr;
        const unsigned long value =
            *text == "all" ? 0 : std::strtoul(text->c_str(), &end, DECIMAL_BASE);
        if (*text != "all" && (value == 0 || end == nullptr || *end != '\0')) {
            return usage(findMessage(MessageKey::HISTORY_DEPTH_USAGE));
        }
        depth = *text == "all" ? UINT32_MAX : static_cast<uint32_t>(value);
    }
    printRow(CHECKPOINT_HEADER);
    KiottySpecCheckpointHandle handle = KIOTTY_SPEC_CHECKPOINT_CURRENT;
    for (uint32_t count = 0; count < depth; ++count) {
        const KiottySpecCheckpointResult checkpoint =
            KiottySpec_readCheckpoint(session.context, handle);
        if (checkpoint.code == KIOTTY_SPEC_STATUS_NOT_FOUND) { break; }
        if (checkpoint.code != KIOTTY_SPEC_STATUS_SUCCESS) {
            return fail(session, checkpoint.code);
        }
        printCheckpoint(checkpoint.handle, checkpoint.value);
        if (checkpoint.value.previous == 0) { break; }
        handle = checkpoint.value.previous;
    }
    return 0;
}

int showDecision(const Session& session)
{
    const std::string* name = positional(session, 0);
    const std::optional<KiottySpecItemKind> kind =
        name == nullptr ? std::nullopt : kindArgument(session, *name);
    if (!kind) { return usage(findMessage(MessageKey::DECISION_SHOW_USAGE)); }
    std::vector<char> buffer(KIOTTY_SPEC_MAX_DECISION_LENGTH + 1);
    const KiottySpecSizeResult read = KiottySpec_readDecision(
        session.context, *kind, KiottySpecBuffer { buffer.data(), buffer.size() });
    if (read.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, read.code); }
    std::fwrite(buffer.data(), 1, read.value, stdout);
    if (read.value > 0 && buffer[read.value - 1] != '\n') { std::fputc('\n', stdout); }
    return 0;
}

int setDecision(const Session& session)
{
    const std::string* name = positional(session, 0);
    const std::optional<KiottySpecItemKind> kind =
        name == nullptr ? std::nullopt : kindArgument(session, *name);
    const BodyOption text = bodyOption(session, "text", "file");
    if (text.is_unreadable) { return USAGE_EXIT_CODE; }
    const bool has_arguments = kind.has_value() && text.text.has_value();
    if (!has_arguments) { return usage(findMessage(MessageKey::DECISION_SET_USAGE)); }
    return check(session,
                 KiottySpec_writeDecision(session.context, *kind, textOf(*text.text)).code);
}

int printDiffSummary(const Session& session, const KiottySpecDiffQuery& query)
{
    const KiottySpecDiffSummaryResult summary = KiottySpec_summarizeDiff(session.context, &query);
    if (summary.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, summary.code); }
    printRow({ "kind", "added", "modified", "removed", "relations_added", "relations_removed",
               "decision" });
    for (uint32_t index = 0; index < KIOTTY_SPEC_ITEM_KIND_COUNT; ++index) {
        const KiottySpecKindDiff& kind = summary.kinds[index];
        const uint32_t changes = kind.added + kind.modified + kind.removed + kind.relations_added +
                                 kind.relations_removed;
        if (changes == 0 && kind.decision_changed == KIOTTY_SPEC_FALSE) { continue; }
        printRow({ kindName(static_cast<KiottySpecItemKind>(index + 1)), std::to_string(kind.added),
                   std::to_string(kind.modified), std::to_string(kind.removed),
                   std::to_string(kind.relations_added), std::to_string(kind.relations_removed),
                   kind.decision_changed == KIOTTY_SPEC_TRUE ? "changed" : "-" });
    }
    return 0;
}

int showModel(const Session& session)
{
    const std::string* name = positional(session, 0);
    if (name) { return showKindModel(session, *name); }
    printRow({"kind", "name", "prefix", "step", "tags", "relations"});
    for (uint32_t value = 1; value <= KIOTTY_SPEC_ITEM_KIND_COUNT; ++value) {
        const KiottySpecModelResult model =
            KiottySpec_readModel(session.context, static_cast<KiottySpecItemKind>(value));
        printRow({ kindName(model.value.kind), model.value.name, model.value.id_prefix,
                   stepName(model.value.created_in), std::to_string(model.value.tag_count),
                   std::to_string(model.value.relation_count) });
    }
    return 0;
}

namespace {

void printCheckpoint(KiottySpecCheckpointHandle handle, const KiottySpecCheckpoint& checkpoint)
{
    printRow({ checkpointName(handle), stepName(checkpoint.step), stateName(checkpoint.state),
               cell(checkpoint.description), std::to_string(checkpoint.created_at),
               checkpointName(checkpoint.previous) });
}

int showKindModel(const Session& session, const std::string& name)
{
    const std::optional<KiottySpecItemKind> kind = kindArgument(session, name);
    if (!kind) { return USAGE_EXIT_CODE; }
    const KiottySpecModelResult model = KiottySpec_readModel(session.context, *kind);
    printRow({"type", "index", "name", "values", "cardinality"});
    for (uint32_t index = 0; index < model.value.tag_count; ++index) {
        printTagDefinition(session, *kind, index);
    }
    for (uint32_t index = 0; index < model.value.relation_count; ++index) {
        printRelationDefinition(session, *kind, index);
    }
    return 0;
}

void printTagDefinition(const Session& session, KiottySpecItemKind kind, uint32_t index)
{
    KiottySpecTagDefinition definition {};
    KiottySpec_readTagDefinition(session.context, kind, index, &definition);
    uint64_t named = 0;
    for (size_t bit = 0; bit < KIOTTY_SPEC_TAG_BIT_COUNT; ++bit) {
        const bool is_named = definition.bit_names[bit][0] != 0;
        if (is_named) { named |= uint64_t{1} << bit; }
    }
    printRow({ "tag", std::to_string(index), definition.name, bitNames(definition, named),
               describeCardinality(definition.cardinality) });
}

void printRelationDefinition(const Session& session, KiottySpecItemKind kind, uint32_t index)
{
    const KiottySpecRelationDefinitionResult definition =
        KiottySpec_readRelationDefinition(session.context, kind, index);
    printRow({ "relation", std::to_string(index), definition.value.name,
               kindName(definition.value.target_kind),
               describeCardinality(definition.value.cardinality) });
}

std::string describeCardinality(KiottySpecCardinality cardinality)
{ return cardinality == KIOTTY_SPEC_CARDINALITY_SINGLE ? "single" : "many"; }

}

}
