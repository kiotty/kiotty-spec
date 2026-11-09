#include "kiotty_session.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace kiotty::cli {

namespace {

constexpr uint32_t DEFAULT_IMPACT_DEPTH = 1;
constexpr size_t REPORT_LENGTH = 64 * 1024;
const char* const STEP_NAMES[] = {"", "변경 요청 접수", "요구사항", "외부 인터페이스", "설계", "유닛 테스트 계획", "개발 계획",
                                  "구현", "시스템 테스트 계획", "시스템 테스트 실행", "코드 검사", "E2E 테스트 계획", "E2E 테스트 실행"};

int check(Session& session, KiottySpecStatus status)
{
    return status == KIOTTY_SPEC_STATUS_SUCCESS ? 0 : fail(session, status);
}

const std::string* positional(Session& session, size_t index)
{
    return index < session.args.positional.size() ? &session.args.positional[index] : nullptr;
}

std::string stepName(uint32_t step)
{
    if (step == 0 || step > KIOTTY_SPEC_STEP_COUNT) { return "-"; }
    return std::to_string(step) + " " + STEP_NAMES[step];
}

std::optional<std::string> bodyOption(Session& session, const char* text_name, const char* file_name, bool& failed)
{
    failed = false;
    if (const auto text = session.args.option(text_name)) { return text; }
    if (const auto file = session.args.option(file_name)) {
        const auto content = readFileText(*file);
        if (!content) {
            std::cerr << "error: 파일을 읽을 수 없음: " << *file << "\n";
            failed = true;
        }
        return content;
    }
    return std::nullopt;
}

// 항목 목록의 선택 열 (설계서 v1 8.4)
struct Column {
    std::string header;
    bool is_tag {false};
    TagColumn tag {};
    std::map<std::string, std::string> targets;
};

struct ColumnPlan {
    std::vector<Column> columns;
    std::vector<KiottySpecTagQuery> queries;
};

std::optional<ColumnPlan> planColumns(Session& session, KiottySpecItemKind kind)
{
    ColumnPlan plan;
    auto addTag = [&](const TagColumn& tag) {
        for (const Column& column : plan.columns) {
            if (column.is_tag && column.tag.index == tag.index) { return; }
        }
        Column column;
        column.header = tag.definition.name;
        column.is_tag = true;
        column.tag = tag;
        plan.columns.push_back(column);
    };
    for (const std::string& spec : session.args.all("tag")) {
        const size_t equal = spec.find('=');
        const auto tag = findTag(session, kind, spec.substr(0, equal));
        if (!tag) { return std::nullopt; }
        if (equal != std::string::npos) {
            const auto mask = parseBits(tag->definition, spec.substr(equal + 1));
            if (!mask) { return std::nullopt; }
            if (plan.queries.size() == KIOTTY_SPEC_MAX_TAG_QUERIES) {
                std::cerr << "error: 태그 조건은 " << KIOTTY_SPEC_MAX_TAG_QUERIES << " 개까지\n";
                return std::nullopt;
            }
            plan.queries.push_back(KiottySpecTagQuery{kind, tag->index, *mask});
        }
        addTag(*tag);
    }
    const KiottySpecModelResult model = KiottySpec_readModel(session.context, kind);
    if (session.args.flag("tags")) {
        for (uint32_t index = 0; index < model.value.tag_count; ++index) {
            TagColumn tag {index, {}};
            if (KiottySpec_readTagDefinition(session.context, kind, index, &tag.definition).code == KIOTTY_SPEC_STATUS_SUCCESS) { addTag(tag); }
        }
    }
    std::vector<uint32_t> relations;
    for (const std::string& name : session.args.all("relation")) {
        const auto relation = findRelation(session, kind, name);
        if (!relation) { return std::nullopt; }
        relations.push_back(*relation);
    }
    if (session.args.flag("relations")) {
        for (uint32_t index = 0; index < model.value.relation_count; ++index) { relations.push_back(index); }
    }
    for (const uint32_t relation : relations) {
        Column column;
        column.header = KiottySpec_readRelationDefinition(session.context, kind, relation).value.name;
        const KiottySpecCursorResult cursor = KiottySpec_openRelationCursorByModel(session.context, kind, relation);
        if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) {
            fail(session, cursor.code);
            return std::nullopt;
        }
        KiottySpecRelationPage page {};
        while (KiottySpec_fetchRelationPage(session.context, cursor.value, &page).code == KIOTTY_SPEC_STATUS_SUCCESS && page.length > 0) {
            for (size_t index = 0; index < page.length; ++index) {
                std::string& targets = column.targets[itemId(session, page.elements[index].from)];
                if (!targets.empty()) { targets += ','; }
                targets += itemId(session, page.elements[index].to);
            }
        }
        KiottySpec_closeCursor(session.context, cursor.value);
        plan.columns.push_back(column);
    }
    return plan;
}

void printItemHeader(const ColumnPlan* plan)
{
    std::vector<std::string> header = {"id", "kind", "title", "summary", "checkpoint", "parent"};
    if (plan != nullptr) {
        for (const Column& column : plan->columns) { header.push_back(column.header); }
    }
    printRow(header);
}

void printItem(Session& session, const KiottySpecItem& item, const ColumnPlan* plan)
{
    std::vector<std::string> row = {item.id, KiottySpec_kindName(item.kind), cell(item.title), cell(item.summary), checkpointName(item.checkpoint),
                                    itemId(session, item.parent)};
    if (plan != nullptr) {
        const KiottySpecItemHandleResult handle = KiottySpec_findItem(session.context, textOf(item.id));
        for (const Column& column : plan->columns) {
            if (column.is_tag) {
                const KiottySpecTagResult tag = KiottySpec_readItemTag(session.context, handle.value, column.tag.index);
                row.push_back(bitNames(column.tag.definition, tag.code == KIOTTY_SPEC_STATUS_SUCCESS ? tag.value.value : 0));
                continue;
            }
            const auto found = column.targets.find(item.id);
            row.push_back(found == column.targets.end() ? "-" : found->second);
        }
    }
    printRow(row);
}

int printItemCursor(Session& session, const KiottySpecCursorResult& cursor, const ColumnPlan* plan)
{
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, cursor.code); }
    printItemHeader(plan);
    KiottySpecItemPage page {};
    while (true) {
        const KiottySpecStatusResult fetched = KiottySpec_fetchItemPage(session.context, cursor.value, &page);
        if (fetched.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, fetched.code); }
        if (page.length == 0) { break; }
        for (size_t index = 0; index < page.length; ++index) { printItem(session, page.elements[index], plan); }
    }
    KiottySpec_closeCursor(session.context, cursor.value);
    return 0;
}

int initProject(Session& session)
{
    const std::string name = session.args.option("name").value_or("");
    return check(session, KiottySpec_initProject(session.context, textOf(name), session.args.flag("hooks")).code);
}

int setTags(Session& session, const std::string& id, KiottySpecItemKind kind)
{
    for (const std::string& spec : session.args.all("tag")) {
        const size_t equal = spec.find('=');
        if (equal == std::string::npos) { return usage("--tag 이름=값[,값]"); }
        const auto tag = findTag(session, kind, spec.substr(0, equal));
        if (!tag) { return 2; }
        const auto value = parseBits(tag->definition, spec.substr(equal + 1));
        if (!value) { return 2; }
        const KiottySpecItemHandleResult handle = KiottySpec_findItem(session.context, textOf(id));
        if (handle.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, handle.code); }
        const KiottySpecStatusResult set = KiottySpec_setItemTag(session.context, handle.value, tag->index, *value);
        if (set.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, set.code); }
    }
    return 0;
}

int addItem(Session& session)
{
    const std::string* kind_name = positional(session, 0);
    const auto title = session.args.option("title");
    if (kind_name == nullptr || !title) { return usage("ks item add KIND --title T"); }
    const auto kind = kindArgument(*kind_name);
    if (!kind) { return usage("알 수 없는 Kind: " + *kind_name + ". `ks model` 참고"); }
    bool failed = false;
    const auto body = bodyOption(session, "body", "body-file", failed);
    if (failed) { return 2; }
    const std::string summary = session.args.option("summary").value_or("");
    const KiottySpecIdResult created = KiottySpec_createItem(session.context, *kind, textOf(*title), textOf(summary));
    if (created.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, created.code); }
    const std::string id = created.value;
    if (const auto parent = session.args.option("parent")) {
        const KiottySpecStatusResult set = KiottySpec_setItemParent(session.context, textOf(id), textOf(*parent));
        if (set.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, set.code); }
    }
    if (body) {
        const KiottySpecStatusResult written = KiottySpec_writeItemBody(session.context, textOf(id), textOf(*body));
        if (written.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, written.code); }
    }
    const int tagged = setTags(session, id, *kind);
    if (tagged != 0) { return tagged; }
    std::printf("%s\n", id.c_str());
    return 0;
}

int setItem(Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) { return usage("ks item set ID [--title T] [--summary S] [--parent ID|-] [--body T|--body-file F] [--tag 이름=값]"); }
    const KiottySpecItemResult item = KiottySpec_readItem(session.context, textOf(*id));
    if (item.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, item.code); }
    const auto title = session.args.option("title");
    const auto summary = session.args.option("summary");
    if (title || summary) {
        const KiottySpecText title_text = title ? textOf(*title) : KiottySpecText{nullptr, 0};
        const KiottySpecText summary_text = summary ? textOf(*summary) : KiottySpecText{nullptr, 0};
        const KiottySpecStatusResult updated = KiottySpec_updateItem(session.context, textOf(*id), title_text, summary_text);
        if (updated.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, updated.code); }
    }
    if (const auto parent = session.args.option("parent")) {
        const std::string target = *parent == "-" ? std::string() : *parent;
        const KiottySpecStatusResult set = KiottySpec_setItemParent(session.context, textOf(*id), textOf(target));
        if (set.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, set.code); }
    }
    bool failed = false;
    const auto body = bodyOption(session, "body", "body-file", failed);
    if (failed) { return 2; }
    if (body) {
        const KiottySpecStatusResult written = KiottySpec_writeItemBody(session.context, textOf(*id), textOf(*body));
        if (written.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, written.code); }
    }
    return setTags(session, *id, item.value.kind);
}

int showItem(Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) { return usage("ks item show ID [--tag 이름]... [--tags] [--relation 이름]... [--relations]"); }
    const KiottySpecItemResult item = KiottySpec_readItem(session.context, textOf(*id));
    if (item.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, item.code); }
    const auto plan = planColumns(session, item.value.kind);
    if (!plan) { return 2; }
    printItemHeader(&*plan);
    printItem(session, item.value, &*plan);
    return 0;
}

int listItems(Session& session)
{
    KiottySpecItemKind kind = static_cast<KiottySpecItemKind>(0);
    if (const auto name = session.args.option("kind")) {
        const auto parsed = kindArgument(*name);
        if (!parsed) { return usage("알 수 없는 Kind: " + *name + ". `ks model` 참고"); }
        kind = *parsed;
    }
    const bool wants_columns = !session.args.all("tag").empty() || !session.args.all("relation").empty() || session.args.flag("tags") ||
                               session.args.flag("relations");
    if (kind == 0 && wants_columns) { return usage("--tag · --relation 은 --kind 와 함께 쓴다"); }
    std::optional<ColumnPlan> plan;
    if (kind != 0) {
        plan = planColumns(session, kind);
        if (!plan) { return 2; }
    }
    const KiottySpecTagQuery* queries = plan && !plan->queries.empty() ? plan->queries.data() : nullptr;
    const size_t count = plan ? plan->queries.size() : 0;
    return printItemCursor(session, KiottySpec_openItemCursor(session.context, kind, queries, count), plan ? &*plan : nullptr);
}

int listChildren(Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) { return usage("ks item children ID"); }
    return printItemCursor(session, KiottySpec_openChildCursor(session.context, textOf(*id)), nullptr);
}

int showBody(Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) { return usage("ks item body ID"); }
    std::vector<char> buffer(KIOTTY_SPEC_MAX_BODY_LENGTH + 1);
    const KiottySpecSizeResult read = KiottySpec_readItemBody(session.context, textOf(*id), buffer.data(), buffer.size());
    if (read.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, read.code); }
    std::fwrite(buffer.data(), 1, read.value, stdout);
    if (read.value > 0 && buffer[read.value - 1] != '\n') { std::fputc('\n', stdout); }
    return 0;
}

int removeItem(Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) { return usage("ks item remove ID"); }
    return check(session, KiottySpec_removeItem(session.context, textOf(*id)).code);
}

int showTags(Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) { return usage("ks tag show ID"); }
    const KiottySpecItemResult item = KiottySpec_readItem(session.context, textOf(*id));
    if (item.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, item.code); }
    const KiottySpecModelResult model = KiottySpec_readModel(session.context, item.value.kind);
    printRow({"tag", "value"});
    for (uint32_t index = 0; index < model.value.tag_count; ++index) {
        KiottySpecTagDefinition definition {};
        if (KiottySpec_readTagDefinition(session.context, item.value.kind, index, &definition).code != KIOTTY_SPEC_STATUS_SUCCESS) { continue; }
        const KiottySpecTagResult tag = KiottySpec_readItemTag(session.context, item.handle, index);
        printRow({definition.name, bitNames(definition, tag.code == KIOTTY_SPEC_STATUS_SUCCESS ? tag.value.value : 0)});
    }
    return 0;
}

int setTag(Session& session)
{
    const std::string* id = positional(session, 0);
    const std::string* name = positional(session, 1);
    const std::string* bits = positional(session, 2);
    if (id == nullptr || name == nullptr || bits == nullptr) { return usage("ks tag set ID 태그이름 값[,값]|-"); }
    const KiottySpecItemResult item = KiottySpec_readItem(session.context, textOf(*id));
    if (item.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, item.code); }
    const auto tag = findTag(session, item.value.kind, *name);
    if (!tag) { return 2; }
    const auto value = parseBits(tag->definition, *bits);
    if (!value) { return 2; }
    return check(session, KiottySpec_setItemTag(session.context, item.handle, tag->index, *value).code);
}

int changeRelation(Session& session, bool add)
{
    const std::string* from = positional(session, 0);
    const std::string* name = positional(session, 1);
    const std::string* to = positional(session, 2);
    if (from == nullptr || name == nullptr || to == nullptr) { return usage("ks relation add|remove FROM 관계이름 TO"); }
    const KiottySpecItemResult item = KiottySpec_readItem(session.context, textOf(*from));
    if (item.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, item.code); }
    const auto relation = findRelation(session, item.value.kind, *name);
    if (!relation) { return 2; }
    const KiottySpecStatusResult changed = add ? KiottySpec_addRelation(session.context, textOf(*from), *relation, textOf(*to))
                                               : KiottySpec_removeRelation(session.context, textOf(*from), *relation, textOf(*to));
    return check(session, changed.code);
}

int addRelation(Session& session) { return changeRelation(session, true); }
int removeRelation(Session& session) { return changeRelation(session, false); }

int listRelations(Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) { return usage("ks relation list ID [--up|--down]"); }
    KiottySpecDirection direction = KIOTTY_SPEC_DIRECTION_BOTH;
    if (session.args.flag("up")) { direction = KIOTTY_SPEC_DIRECTION_UPSTREAM; }
    if (session.args.flag("down")) { direction = KIOTTY_SPEC_DIRECTION_DOWNSTREAM; }
    const KiottySpecCursorResult cursor = KiottySpec_openRelationCursor(session.context, textOf(*id), direction);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, cursor.code); }
    printRow({"from", "relation", "to"});
    KiottySpecRelationPage page {};
    while (KiottySpec_fetchRelationPage(session.context, cursor.value, &page).code == KIOTTY_SPEC_STATUS_SUCCESS && page.length > 0) {
        for (size_t index = 0; index < page.length; ++index) {
            const KiottySpecRelation& relation = page.elements[index];
            const KiottySpecRelationDefinitionResult definition = KiottySpec_readRelationDefinition(session.context, relation.model, relation.relation);
            printRow({itemId(session, relation.from), definition.value.name, itemId(session, relation.to)});
        }
    }
    KiottySpec_closeCursor(session.context, cursor.value);
    return 0;
}

int showImpact(Session& session)
{
    const std::string* id = positional(session, 0);
    if (id == nullptr) { return usage("ks impact ID [--depth N]"); }
    uint32_t depth = DEFAULT_IMPACT_DEPTH;
    if (const auto text = session.args.option("depth")) { depth = static_cast<uint32_t>(std::strtoul(text->c_str(), nullptr, 10)); }
    const KiottySpecCursorResult cursor = KiottySpec_openImpactCursor(session.context, textOf(*id), depth);
    if (cursor.code != KIOTTY_SPEC_STATUS_SUCCESS) { return fail(session, cursor.code); }
    printRow({"id"});
    KiottySpecIdPage page {};
    while (KiottySpec_fetchIdPage(session.context, cursor.value, &page).code == KIOTTY_SPEC_STATUS_SUCCESS && page.length > 0) {
        for (size_t index = 0; index < page.length; ++index) { printRow({page.elements[index]}); }
    }
    KiottySpec_closeCursor(session.context, cursor.value);
    return 0;
}

void printCheckpoint(KiottySpecCheckpointHandle handle, const KiottySpecCheckpoint& checkpoint)
{
    printRow({checkpointName(handle), stepName(checkpoint.step), KiottySpec_checkpointStateName(checkpoint.state), cell(checkpoint.description),
              std::to_string(checkpoint.created_at), checkpointName(checkpoint.previous)});
}

const std::vector<std::string> CHECKPOINT_HEADER = {"checkpoint", "step", "state", "description", "created_at", "previous"};

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

const std::vector<CommandEntry>& commands()
{
    static const std::vector<CommandEntry> table = {
        {"init", "", "[--name N] [--hooks]", "프로젝트 메타 · .gitignore 를 쓰고 git 훅을 설치", initProject},
        {"item", "add", "KIND --title T [--summary S] [--parent ID] [--body T|--body-file F] [--tag 이름=값]...", "항목을 만들고 ID 를 찍는다", addItem},
        {"item", "set", "ID [--title T] [--summary S] [--parent ID|-] [--body T|--body-file F] [--tag 이름=값]...", "항목을 고친다", setItem},
        {"item", "show", "ID [--tag 이름]... [--tags] [--relation 이름]... [--relations]", "항목 한 줄", showItem},
        {"item", "list", "[--kind K] [--tag 이름[=값,...]]... [--tags] [--relation 이름]... [--relations]", "항목 목록 (8.3, 8.4)", listItems},
        {"item", "children", "ID", "하위 항목 목록", listChildren},
        {"item", "body", "ID", "본문 (본문을 찍는 유일한 명령, 3.7)", showBody},
        {"item", "remove", "ID", "항목을 지운다", removeItem},
        {"tag", "show", "ID", "항목의 태그 값", showTags},
        {"tag", "set", "ID 태그이름 값[,값]|-", "태그 값을 바꾼다. - 는 지움", setTag},
        {"relation", "add", "FROM 관계이름 TO", "관계를 더한다", addRelation},
        {"relation", "remove", "FROM 관계이름 TO", "관계를 지운다", removeRelation},
        {"relation", "list", "ID [--up|--down]", "항목의 관계 (깊이 1)", listRelations},
        {"impact", "", "ID [--depth N]", "이 항목을 가리키는 하류 항목 ID", showImpact},
        {"checkpoint", "start", "STEP [--description D]", "체크포인트를 시작한다 (STARTED)", startCheckpoint},
        {"checkpoint", "resume", "", "가장 최근 체크포인트를 RUNNING 으로", resumeCheckpoint},
        {"checkpoint", "complete", "--actor user", "확정 조건을 검사하고 DONE 으로 (사용자만)", completeCheckpoint},
        {"checkpoint", "show", "[CP]", "체크포인트 한 줄 (기본: 가장 최근)", showCheckpoint},
        {"checkpoint", "list", "[--step N]", "체크포인트 목록", listCheckpoints},
        {"checkpoint", "prune", "CP --actor user", "CP 이전 기록을 지운다 (사용자만)", pruneCheckpoints},
        {"where", "", "", "현재 위치와 다음 단계", showPosition},
        {"journey", "", "", "지나온 단계 구간", showJourney},
        {"decision", "show", "KIND", "결정 기록", showDecision},
        {"decision", "set", "KIND (--text T | --file F)", "결정 기록을 다시 쓴다 (2048 바이트 이하)", setDecision},
        {"diff", "", "FROM TO [--kind K (--added|--removed|--modified)... [--title] [--summary] [--tag 이름]... [--tags] [--relation 이름]... [--relations] [--no-color]]",
         "체크포인트 사이 비교. --kind 가 없으면 Kind 별 요약 (5.4)", runDiff},
        {"model", "", "[KIND]", "Kind 목록, 또는 Kind 의 태그 · 관계 정의", showModel},
        {"export", "", "jsonl [DIR]", "DB 를 jsonl 로 (기본 .kiotty/spec)", exportJsonl},
        {"import", "", "jsonl [DIR]", "jsonl 로 DB 를 다시 만든다. 옛 형식은 변환한다", importJsonl},
    };
    return table;
}

}
