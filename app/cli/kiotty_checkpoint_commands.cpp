#include "kiotty_commands.h"

#include <cstdio>
#include <iostream>

namespace kiotty::cli {

namespace {

constexpr size_t REPORT_LENGTH = 64 * 1024;
const std::vector<std::string> CHECKPOINT_HEADER = {"checkpoint", "step", "state", "description", "created_at", "previous"};

void printCheckpoint(KiottySpecCheckpointHandle handle, const KiottySpecCheckpoint& checkpoint)
{
    printRow({checkpointName(handle), stepName(checkpoint.step), KiottySpec_checkpointStateName(checkpoint.state), cell(checkpoint.description),
              std::to_string(checkpoint.created_at), checkpointName(checkpoint.previous)});
}

}

int startCheckpoint(Session& session)
{
    const std::string* step = positional(session, 0);
    if (step == nullptr) { return usage("ks checkpoint start STEP(1~12) [--description D]"); }
    const unsigned long value = std::strtoul(step->c_str(), nullptr, 10);
    const std::string description = session.args.option("description").value_or("");
    const KiottySpecCheckpointHandleResult started = KiottySpec_startCheckpoint(session.context, static_cast<KiottySpecStep>(value), textOf(description));
    if (started.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, started.code); }
    std::printf("%s\n", checkpointName(started.value).c_str());
    return 0;
}

int resumeCheckpoint(Session& session) { return check(session, KiottySpec_resumeCheckpoint(session.context).code); }

int completeCheckpoint(Session& session) { return check(session, KiottySpec_completeCheckpoint(session.context, session.actor).code); }

int showCheckpoint(Session& session)
{
    KiottySpecCheckpointHandle handle = KIOTTY_SPEC_CHECKPOINT_CURRENT;
    if (const std::string* name = positional(session, 0)) {
        const auto parsed = parseCheckpoint(*name);
        if (!parsed) { return usage("체크포인트는 CP-0003 또는 3"); }
        handle = *parsed;
    }
    const KiottySpecCheckpointResult checkpoint = KiottySpec_readCheckpoint(session.context, handle);
    if (checkpoint.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, checkpoint.code); }
    printRow(CHECKPOINT_HEADER);
    printCheckpoint(checkpoint.handle, checkpoint.value);
    return 0;
}

int listCheckpoints(Session& session)
{
    uint32_t step = 0;
    if (const auto text = session.args.option("step")) { step = static_cast<uint32_t>(std::strtoul(text->c_str(), nullptr, 10)); }
    const KiottySpecCursorResult cursor = KiottySpec_openCheckpointCursor(session.context, static_cast<KiottySpecStep>(step));
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, cursor.code); }
    printRow(CHECKPOINT_HEADER);
    KiottySpecCheckpointPage page {};
    while (KiottySpec_fetchCheckpointPage(session.context, cursor.value, &page).code == KIOTTY_SPEC_STATUS_SUCCESS && page.length > 0) {
        for (size_t index = 0; index < page.length; ++index) { printCheckpoint(page.handles[index], page.elements[index]); }
    }
    KiottySpec_closeCursor(session.context, cursor.value);
    return 0;
}

int pruneCheckpoints(Session& session)
{
    const std::string* name = positional(session, 0);
    const auto base = name == nullptr ? std::nullopt : parseCheckpoint(*name);
    if (!base) { return usage("ks checkpoint prune CP --actor user"); }
    const KiottySpecSizeResult pruned = KiottySpec_pruneCheckpoints(session.context, session.actor, *base);
    if (pruned.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, pruned.code); }
    printRow({"removed"});
    printRow({std::to_string(pruned.value)});
    return 0;
}

int showPosition(Session& session)
{
    const KiottySpecPositionResult position = KiottySpec_readPosition(session.context);
    if (position.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, position.code); }
    const KiottySpecPosition& value = position.value;
    printRow({"checkpoint", "step", "state", "pending_writes", "open_change_requests", "next_step"});
    printRow({checkpointName(value.current), stepName(value.step), value.state == 0 ? "-" : KiottySpec_checkpointStateName(value.state),
              std::to_string(value.pending_writes), std::to_string(value.open_change_requests), stepName(value.next_step)});
    return 0;
}

int showJourney(Session& session)
{
    const KiottySpecCursorResult cursor = KiottySpec_openStepRunCursor(session.context);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, cursor.code); }
    printRow({"step", "first", "last", "count", "state"});
    KiottySpecStepRunPage page {};
    while (KiottySpec_fetchStepRunPage(session.context, cursor.value, &page).code == KIOTTY_SPEC_STATUS_SUCCESS && page.length > 0) {
        for (size_t index = 0; index < page.length; ++index) {
            const KiottySpecStepRun& run = page.elements[index];
            printRow({stepName(run.step), checkpointName(run.first), checkpointName(run.last), std::to_string(run.count),
                      KiottySpec_checkpointStateName(run.state)});
        }
    }
    KiottySpec_closeCursor(session.context, cursor.value);
    return 0;
}

int showDecision(Session& session)
{
    const std::string* name = positional(session, 0);
    const auto kind = name == nullptr ? std::nullopt : kindArgument(*name);
    if (!kind) { return usage("ks decision show KIND"); }
    std::vector<char> buffer(KIOTTY_SPEC_MAX_DECISION_LENGTH + 1);
    const KiottySpecSizeResult read = KiottySpec_readDecision(session.context, *kind, buffer.data(), buffer.size());
    if (read.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, read.code); }
    std::fwrite(buffer.data(), 1, read.value, stdout);
    if (read.value > 0 && buffer[read.value - 1] != '\n') { std::fputc('\n', stdout); }
    return 0;
}

int setDecision(Session& session)
{
    const std::string* name = positional(session, 0);
    const auto kind = name == nullptr ? std::nullopt : kindArgument(*name);
    bool failed = false;
    const auto text = bodyOption(session, "text", "file", failed);
    if (failed) { return 2; }
    if (!kind || !text) { return usage("ks decision set KIND (--text T | --file F)"); }
    return check(session, KiottySpec_writeDecision(session.context, *kind, textOf(*text)).code);
}

int showModel(Session& session)
{
    if (const std::string* name = positional(session, 0)) {
        const auto kind = kindArgument(*name);
        if (!kind) { return usage("알 수 없는 Kind: " + *name); }
        const KiottySpecModelResult model = KiottySpec_readModel(session.context, *kind);
        printRow({"type", "index", "name", "values", "cardinality"});
        for (uint32_t index = 0; index < model.value.tag_count; ++index) {
            KiottySpecTagDefinition definition {};
            KiottySpec_readTagDefinition(session.context, *kind, index, &definition);
            uint64_t named = 0;
            for (size_t bit = 0; bit < KIOTTY_SPEC_TAG_BIT_COUNT; ++bit) {
                if (definition.bit_names[bit][0] != 0) { named |= uint64_t{1} << bit; }
            }
            printRow({"tag", std::to_string(index), definition.name, bitNames(definition, named),
                      definition.cardinality == KIOTTY_SPEC_CARDINALITY_SINGLE ? "single" : "many"});
        }
        for (uint32_t index = 0; index < model.value.relation_count; ++index) {
            const KiottySpecRelationDefinitionResult definition = KiottySpec_readRelationDefinition(session.context, *kind, index);
            printRow({"relation", std::to_string(index), definition.value.name, KiottySpec_kindName(definition.value.target_kind),
                      definition.value.cardinality == KIOTTY_SPEC_CARDINALITY_SINGLE ? "single" : "many"});
        }
        return 0;
    }
    printRow({"kind", "name", "prefix", "step", "tags", "relations"});
    for (uint32_t value = 1; value <= KIOTTY_SPEC_ITEM_KIND_COUNT; ++value) {
        const KiottySpecModelResult model = KiottySpec_readModel(session.context, static_cast<KiottySpecItemKind>(value));
        printRow({KiottySpec_kindName(model.value.kind), model.value.name, model.value.id_prefix, stepName(model.value.created_in),
                  std::to_string(model.value.tag_count), std::to_string(model.value.relation_count)});
    }
    return 0;
}

int exportJsonl(Session& session)
{
    const std::string directory = session.args.positional.size() > 1 ? session.args.positional[1] : std::string();
    if (session.args.positional.empty() || session.args.positional[0] != "jsonl") { return usage("ks export jsonl [DIR]"); }
    return check(session, KiottySpec_exportJsonl(session.context, textOf(directory)).code);
}

int importJsonl(Session& session)
{
    const std::string directory = session.args.positional.size() > 1 ? session.args.positional[1] : std::string();
    if (session.args.positional.empty() || session.args.positional[0] != "jsonl") { return usage("ks import jsonl [DIR]"); }
    std::vector<char> report(REPORT_LENGTH);
    const KiottySpecSizeResult imported = KiottySpec_importJsonl(session.context, textOf(directory), report.data(), report.size());
    if (imported.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, imported.code); }
    if (imported.value > 0) { std::fputs(report.data(), stderr); }
    return 0;
}

}
