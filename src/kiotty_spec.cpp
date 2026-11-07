#include "kiotty_spec.h"

#include "implementation/context/project.h"
#include "implementation/data/datasource/schema.h"
#include "implementation/context/cursor.h"
#include "implementation/core/error.h"
#include "implementation/domain/entity/names.h"
#include "implementation/core/text.h"
#include "implementation/core/text_view.h"
#include "implementation/domain/entity/body_template.h"

#include <algorithm>
#include <cstdlib>
#include <new>
#include <set>
#include <string>
#include <type_traits>
#include <variant>

struct KiottySpec {
    explicit KiottySpec(std::string_view root) : project(root) {}
    kiotty::spec::implementation::Project project;
};

namespace {

using namespace kiotty::spec::implementation;

thread_local std::string g_lastError;

template <class Body>
KiottySpecStatus guard(Body &&body) {
    try {
        body();
        g_lastError.clear();
        return KIOTTY_SPEC_STATUS_SUCCESS;
    } catch (const Error &e) {
        g_lastError = e.what();
        return e.status();
    } catch (const std::bad_alloc &) {
        g_lastError = "메모리 부족";
        return KIOTTY_SPEC_STATUS_DATABASE;
    } catch (const std::exception &e) {
        g_lastError = e.what();
        return KIOTTY_SPEC_STATUS_DATABASE;
    } catch (...) {
        g_lastError = "알 수 없는 오류";
        return KIOTTY_SPEC_STATUS_DATABASE;
    }
}

Project &project(KiottySpec *db) {
    require(db != nullptr, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "db 핸들이 NULL");
    return db->project;
}

std::string str(const char *text, const char *what) {
    require(text != nullptr, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, std::string(what) + " 이 NULL");
    return text;
}

SystemPath jsonlTarget(Project &p, const char *dir) {
    if (dir == nullptr || *dir == '\0') return p.jsonlDir();
    const auto joined = joinPath(boundedText(dir, MAX_SYSTEM_PATH_LENGTH), Span<const char>());
    require(static_cast<bool>(joined), KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, "경로가 너무 김");
    return joined.value;
}

void markRemoved(ItemRecord &record) {
    TitleText marked;
    const std::string title = "[삭제됨] " + record.title;
    if (marked.assign(spanOf(title)) == KIOTTY_SPEC_STATUS_SUCCESS) record.title = marked;
}

std::string exportMarkdown(Project &p, KiottySpecItemKind kind) {
    std::string out = "# " + std::string(label(kind)) + "\n\n";
    const auto items = expect(p.finder.ofKind(kind), p.failure());
    if (items.empty()) return out + "없음\n";
    for (const auto &item : items) {
        out += p.context.renderItem(item, true);
    }
    return out;
}

std::optional<std::string> optStr(const char *text) {
    if (text == nullptr || *text == '\0') return std::nullopt;
    return std::string(text);
}

template <class T>
T *out(T *pointer) {
    require(pointer != nullptr, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "out 인자가 NULL");
    return pointer;
}

void actorOk(KiottySpecActor actor) {
    require(isValidActor(actor), KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "actor 는 User 또는 Ai");
}

int64_t cpSeq(KiottySpec *db, const char *cpId) { return expect(project(db).checkpoints.resolve(str(cpId, "체크포인트 ID")), project(db).failure()); }

template <class Records>
KiottySpecStatus cursorOf(KiottySpecCursor **outCursor, Records &&records) {
    *outCursor = new KiottySpecCursor{boxAll(std::forward<Records>(records))};
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

template <class Box, class View>
KiottySpecStatus cursorGet(KiottySpecCursor *cursor, size_t index, const View **outView) {
    return guard([&] {
        require(cursor != nullptr, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "cursor 가 NULL");
        out(outView);
        auto *list = std::get_if<BoxList<Box>>(&cursor->payload);
        require(list != nullptr, KIOTTY_SPEC_STATUS_WRONG_CURSOR_TYPE, "커서 원소 종류가 다름");
        require(index < list->size(), KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "index 범위 초과");
        *outView = &(*list)[index]->view;
    });
}

template <class Enum>
const char *enumNameOf(int value) {
    return name(static_cast<Enum>(value)).data();
}

template <class Enum>
bool enumParseOf(std::string_view text, int &out) {
    Enum value{};
    if (!parse(text, value)) return false;
    out = static_cast<int>(value);
    return true;
}

const char *directionName(int value) {
    switch (value) {
    case KIOTTY_SPEC_DIRECTION_UPSTREAM: return "up";
    case KIOTTY_SPEC_DIRECTION_DOWNSTREAM: return "down";
    case KIOTTY_SPEC_DIRECTION_BOTH: return "both";
    default: return "unknown";
    }
}

const char *actorName(int value) {
    switch (value) {
    case KIOTTY_SPEC_ACTOR_USER: return "user";
    case KIOTTY_SPEC_ACTOR_AI: return "ai";
    default: return "unknown";
    }
}

}

extern "C" {

KiottySpecStatus KiottySpec_open(const char *projectRoot, KiottySpec **outDb) {
    return guard([&] {
        out(outDb);
        *outDb = nullptr;
        *outDb = new KiottySpec(str(projectRoot, "projectRoot"));
    });
}

KiottySpecStatus KiottySpec_close(KiottySpec *db) {
    return guard([&] { delete db; });
}

KiottySpecStatus KiottySpec_init(KiottySpec *db, const char *projectName, bool installGitHooks) {
    return guard([&] { project(db).init(projectName != nullptr ? projectName : "", installGitHooks); });
}

KiottySpecStatus KiottySpec_setPolicy(KiottySpec *db, const char *key, const char *value) {
    return guard([&] { expect(project(db).meta.setPolicy(str(key, "key"), str(value, "value")), project(db).failure()); });
}

KiottySpecStatus KiottySpec_getPolicy(KiottySpec *db, const char *key, char **outValue) {
    return guard([&] {
        out(outValue);
        const auto value = expect(project(db).meta.policy(str(key, "key")), project(db).failure());
        require(value.has_value(), KIOTTY_SPEC_STATUS_NOT_FOUND, "정책 없음: " + std::string(key));
        *outValue = duplicateString(*value);
    });
}

KiottySpecStatus KiottySpec_Transaction_start(KiottySpec *db) {
    return guard([&] { project(db).beginTransaction(); });
}

KiottySpecStatus KiottySpec_Transaction_commit(KiottySpec *db) {
    return guard([&] { project(db).commitTransaction(); });
}

KiottySpecStatus KiottySpec_Transaction_rollback(KiottySpec *db) {
    return guard([&] { project(db).rollbackTransaction(); });
}

void KiottySpec_free(void *ptr) { std::free(ptr); }

const char *KiottySpec_strerror(KiottySpecStatus status) { return name(status).data(); }

const char *KiottySpec_lastError(void) { return g_lastError.c_str(); }

const char *KiottySpec_enumName(const char *enumType, int value) {
    const std::string_view type = enumType != nullptr ? enumType : "";
    if (type == "ItemKind") return enumNameOf<KiottySpecItemKind>(value);
    if (type == "ItemCategory") return enumNameOf<KiottySpecItemCategory>(value);
    if (type == "FileRole") return enumNameOf<KiottySpecFileRole>(value);
    if (type == "LinkType") return enumNameOf<KiottySpecLinkType>(value);
    if (type == "ChangeSource") return enumNameOf<KiottySpecChangeSource>(value);
    if (type == "ChangeState") return enumNameOf<KiottySpecChangeState>(value);
    if (type == "ImplState") return enumNameOf<KiottySpecImplementationState>(value);
    if (type == "TestResult") return enumNameOf<KiottySpecTestResult>(value);
    if (type == "Scope") return enumNameOf<KiottySpecScope>(value);
    if (type == "DecisionKind") return enumNameOf<KiottySpecDecisionKind>(value);
    if (type == "CheckpointPurpose") return enumNameOf<KiottySpecCheckpointPurpose>(value);
    if (type == "Status") return enumNameOf<KiottySpecStatus>(value);
    if (type == "Direction") return directionName(value);
    if (type == "Actor") return actorName(value);
    return "unknown";
}

KiottySpecStatus KiottySpec_enumParse(const char *enumType, const char *name, int *outValue) {
    return guard([&] {
        const std::string type = str(enumType, "enumType");
        const std::string text = str(name, "name");
        int value = 0;
        bool ok = false;
        if (type == "ItemKind") ok = enumParseOf<KiottySpecItemKind>(text, value);
        else if (type == "ItemCategory") ok = enumParseOf<KiottySpecItemCategory>(text, value);
        else if (type == "FileRole") ok = enumParseOf<KiottySpecFileRole>(text, value);
        else if (type == "LinkType") ok = enumParseOf<KiottySpecLinkType>(text, value);
        else if (type == "ChangeSource") ok = enumParseOf<KiottySpecChangeSource>(text, value);
        else if (type == "ChangeState") ok = enumParseOf<KiottySpecChangeState>(text, value);
        else if (type == "ImplState") ok = enumParseOf<KiottySpecImplementationState>(text, value);
        else if (type == "TestResult") ok = enumParseOf<KiottySpecTestResult>(text, value);
        else if (type == "DecisionKind") ok = enumParseOf<KiottySpecDecisionKind>(text, value);
        else if (type == "CheckpointPurpose") ok = enumParseOf<KiottySpecCheckpointPurpose>(text, value);
        else if (type == "Scope") {
            ok = text == "intra_module" || text == "inter_module";
            value = text == "intra_module" ? KIOTTY_SPEC_SCOPE_INTRA_MODULE : KIOTTY_SPEC_SCOPE_INTER_MODULE;
        } else if (type == "Direction") {
            for (int candidate = KIOTTY_SPEC_DIRECTION_UPSTREAM; candidate <= KIOTTY_SPEC_DIRECTION_BOTH; ++candidate) {
                if (text == directionName(candidate)) { value = candidate; ok = true; }
            }
        } else if (type == "Actor") {
            for (int candidate = KIOTTY_SPEC_ACTOR_USER; candidate <= KIOTTY_SPEC_ACTOR_AI; ++candidate) {
                if (text == actorName(candidate)) { value = candidate; ok = true; }
            }
        } else {
            kiotty::spec::implementation::fail(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "알 수 없는 enumType: " + type);
        }
        require(ok, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "알 수 없는 " + type + " 이름: '" + text + "'");
        *out(outValue) = value;
    });
}

KiottySpecStatus KiottySpec_Cursor_count(KiottySpecCursor *cursor, size_t *outCount) {
    return guard([&] {
        require(cursor != nullptr, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "cursor 가 NULL");
        *out(outCount) = std::visit([](const auto &list) { return list.size(); }, cursor->payload);
    });
}

KiottySpecStatus KiottySpec_Cursor_getItem(KiottySpecCursor *c, size_t i, const KiottySpecItem **o) { return cursorGet<ItemBox>(c, i, o); }
KiottySpecStatus KiottySpec_Cursor_getLink(KiottySpecCursor *c, size_t i, const KiottySpecLink **o) { return cursorGet<LinkBox>(c, i, o); }
KiottySpecStatus KiottySpec_Cursor_getFile(KiottySpecCursor *c, size_t i, const KiottySpecFile **o) { return cursorGet<FileBox>(c, i, o); }
KiottySpecStatus KiottySpec_Cursor_getCheckpoint(KiottySpecCursor *c, size_t i, const KiottySpecCheckpoint **o) { return cursorGet<CheckpointBox>(c, i, o); }
KiottySpecStatus KiottySpec_Cursor_getChangeRequest(KiottySpecCursor *c, size_t i, const KiottySpecChangeRequest **o) { return cursorGet<ChangeRequestBox>(c, i, o); }
KiottySpecStatus KiottySpec_Cursor_getResult(KiottySpecCursor *c, size_t i, const KiottySpecResult **o) { return cursorGet<ResultBox>(c, i, o); }
KiottySpecStatus KiottySpec_Cursor_getMilestoneEntry(KiottySpecCursor *c, size_t i, const KiottySpecMilestoneEntry **o) { return cursorGet<MilestoneEntryBox>(c, i, o); }
KiottySpecStatus KiottySpec_Cursor_getStepRun(KiottySpecCursor *c, size_t i, const KiottySpecStepRun **o) { return cursorGet<StepRunBox>(c, i, o); }

KiottySpecStatus KiottySpec_Cursor_getId(KiottySpecCursor *cursor, size_t index, const char **outId) {
    return guard([&] {
        require(cursor != nullptr, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "cursor 가 NULL");
        out(outId);
        std::visit(
            [&](const auto &list) {
                require(index < list.size(), KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "index 범위 초과");
                using Box = typename std::decay_t<decltype(list)>::value_type::element_type;
                const auto &box = *list[index];
                if constexpr (std::is_same_v<Box, IdBox>) *outId = box.id.c_str();
                else if constexpr (std::is_same_v<Box, LinkBox>) *outId = box.view.fromId;
                else if constexpr (std::is_same_v<Box, ResultBox>) *outId = box.view.targetId;
                else if constexpr (std::is_same_v<Box, MilestoneEntryBox>) *outId = box.view.itemId;
                else if constexpr (std::is_same_v<Box, StepRunBox>) *outId = box.view.lastCheckpointId;
                else *outId = box.view.id;
            },
            cursor->payload);
    });
}

void KiottySpec_Cursor_end(KiottySpecCursor *cursor) { delete cursor; }

KiottySpecStatus KiottySpec_Checkpoint_create(KiottySpec *db, KiottySpecActor actor, int32_t step, const char *note, char **outId) {
    return guard([&] {
        actorOk(actor);
        out(outId);
        const int64_t seq = expect(project(db).checkpoints.create(step, note != nullptr ? note : "", KIOTTY_SPEC_CHECKPOINT_PURPOSE_NONE), project(db).failure());
        *outId = duplicateString(checkpointName(seq));
    });
}

KiottySpecStatus KiottySpec_Checkpoint_confirm(KiottySpec *db, KiottySpecActor actor, const char *cpId) {
    return guard([&] { project(db).confirm(actor, str(cpId, "cpId")); });
}

KiottySpecStatus KiottySpec_Checkpoint_get(KiottySpec *db, const char *cpId, KiottySpecCheckpoint **outCp) {
    return guard([&] {
        out(outCp);
        const int64_t seq = cpSeq(db, cpId);
        require(seq != kPending, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "PENDING 은 체크포인트가 아님");
        *outCp = packCheckpoint(expect(project(db).checkpoints.get(seq), project(db).failure()));
    });
}

KiottySpecStatus KiottySpec_Checkpoint_current(KiottySpec *db, KiottySpecCheckpoint **outCp) {
    return guard([&] {
        out(outCp);
        const auto latest = expect(project(db).checkpoints.latest(), project(db).failure());
        require(latest.has_value(), KIOTTY_SPEC_STATUS_NOT_FOUND, "체크포인트가 아직 없음");
        *outCp = packCheckpoint(*latest);
    });
}

KiottySpecStatus KiottySpec_Checkpoint_lastConfirmed(KiottySpec *db, int32_t stepOr0, KiottySpecCheckpoint **outCp) {
    return guard([&] {
        out(outCp);
        const auto record = expect(project(db).checkpoints.lastConfirmed(stepOr0), project(db).failure());
        require(record.has_value(), KIOTTY_SPEC_STATUS_NOT_FOUND, "확정된 체크포인트 없음");
        *outCp = packCheckpoint(*record);
    });
}

KiottySpecStatus KiottySpec_Checkpoint_listBegin(KiottySpec *db, int32_t stepOr0, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), expect(project(db).checkpoints.list(stepOr0), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Checkpoint_changedItemsBegin(KiottySpec *db, const char *cpFrom, const char *cpToOrNull, KiottySpecCursor **outCursor) {
    return guard([&] {
        out(outCursor);
        const int64_t from = cpSeq(db, cpFrom);
        const int64_t to = cpToOrNull != nullptr ? cpSeq(db, cpToOrNull) : kCurrent;
        std::vector<ItemRecord> records;
        for (auto &change : expect(project(db).finder.changedBetween(from, to), project(db).failure())) {
            if (change.record.removed) markRemoved(change.record);
            records.push_back(std::move(change.record));
        }
        cursorOf(outCursor, std::move(records));
    });
}

KiottySpecStatus KiottySpec_Checkpoint_diff(KiottySpec *db, const char *cpA, const char *cpB, char **outMarkdown) {
    return guard([&] {
        out(outMarkdown);
        auto &p = project(db);
        const int64_t a = cpSeq(db, cpA);
        const int64_t b = cpSeq(db, cpB);
        std::string md = "# " + checkpointName(a) + " → " + checkpointName(b) + "\n\n## 항목\n";
        const auto changes = expect(p.finder.changedBetween(std::min(a, b), std::max(a, b)), p.failure());
        if (changes.empty()) md += "변경 없음\n";
        for (const auto &change : changes) {
            const char *what = change.record.removed ? "삭제" : change.added ? "추가" : "수정";
            md += "- " + std::string(what) + " " + change.record.id + " " + change.record.title + "\n";
        }
        md += "\n## 링크\n";
        const auto before = expect(p.graph.all(std::min(a, b)), p.failure());
        const auto after = expect(p.graph.all(std::max(a, b)), p.failure());
        const auto key = [](const LinkRecord &l) { return l.fromId + " " + std::string(name(l.type)) + " " + l.toId; };
        std::set<std::string> beforeKeys, afterKeys;
        for (const auto &l : before) beforeKeys.insert(key(l));
        for (const auto &l : after) afterKeys.insert(key(l));
        for (const auto &k : afterKeys) if (beforeKeys.count(k) == 0) md += "- 추가 " + k + "\n";
        for (const auto &k : beforeKeys) if (afterKeys.count(k) == 0) md += "- 삭제 " + k + "\n";
        md += "\n## 결정 기록\n";
        for (const auto kind : {KIOTTY_SPEC_DECISION_KIND_REQUIREMENT, KIOTTY_SPEC_DECISION_KIND_RULE, KIOTTY_SPEC_DECISION_KIND_INTERFACE,
                                KIOTTY_SPEC_DECISION_KIND_DESIGN, KIOTTY_SPEC_DECISION_KIND_UNIT_TEST, KIOTTY_SPEC_DECISION_KIND_PLAN}) {
            if (expect(p.decisions.get(kind, std::min(a, b)), p.failure()) != expect(p.decisions.get(kind, std::max(a, b)), p.failure())) {
                md += "- 바뀜: " + std::string(name(kind)) + "\n";
            }
        }
        *outMarkdown = duplicateString(md);
    });
}

KiottySpecStatus KiottySpec_Checkpoint_confirmCheck(KiottySpec *db, int32_t step, bool *outOk, char **outReport) {
    return guard([&] {
        const auto verdict = expect(project(db).checker.confirmCheck(step), project(db).failure());
        *out(outOk) = verdict.ok;
        *out(outReport) = duplicateString(verdict.report);
    });
}

KiottySpecStatus KiottySpec_Checkpoint_releaseCheck(KiottySpec *db, bool *outOk, char **outReport) {
    return guard([&] {
        const auto verdict = expect(project(db).checker.releaseCheck(), project(db).failure());
        *out(outOk) = verdict.ok;
        *out(outReport) = duplicateString(verdict.report);
    });
}

KiottySpecStatus KiottySpec_Checkpoint_position(KiottySpec *db, KiottySpecPosition **outPosition) {
    return guard([&] { *out(outPosition) = packPosition(expect(project(db).journey.position(), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Checkpoint_journeyBegin(KiottySpec *db, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), expect(project(db).journey.runs(), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Checkpoint_release(KiottySpec *db, KiottySpecActor actor, const char *note, char **outId) {
    return guard([&] {
        out(outId);
        *outId = duplicateString(checkpointName(project(db).release(actor, note != nullptr ? note : "")));
    });
}

KiottySpecStatus KiottySpec_Checkpoint_prune(KiottySpec *db, KiottySpecActor actor, const char *baseCpId, size_t *outRemoved) {
    return guard([&] {
        const size_t removed = project(db).prune(actor, str(baseCpId, "baseCpId"));
        if (outRemoved != nullptr) *outRemoved = removed;
    });
}

KiottySpecStatus KiottySpec_Item_create(KiottySpec *db, KiottySpecActor actor, KiottySpecItemKind kind, KiottySpecItemCategory category,
                                        const char *parentIdOrNull, const char *title, const char *body, char **outId) {
    return guard([&] {
        actorOk(actor);
        out(outId);
        *outId = duplicateString(
            expect(project(db).items.create(kind, category, optStr(parentIdOrNull), str(title, "title"), body != nullptr ? body : ""), project(db).failure()));
    });
}

KiottySpecStatus KiottySpec_Item_get(KiottySpec *db, const char *itemId, KiottySpecItem **outItem) {
    return guard([&] { *out(outItem) = packItem(expect(project(db).items.get(str(itemId, "itemId")), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Item_getAt(KiottySpec *db, const char *itemId, const char *cpId, KiottySpecItem **outItem) {
    return guard([&] { *out(outItem) = packItem(expect(project(db).items.get(str(itemId, "itemId"), cpSeq(db, cpId)), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Item_update(KiottySpec *db, KiottySpecActor actor, const char *itemId, const char *title, const char *body) {
    return guard([&] {
        actorOk(actor);
        expect(project(db).items.update(str(itemId, "itemId"), title != nullptr ? std::optional<std::string>(title) : std::nullopt,
                                 body != nullptr ? std::optional<std::string>(body) : std::nullopt), project(db).failure());
    });
}

KiottySpecStatus KiottySpec_Item_setParent(KiottySpec *db, KiottySpecActor actor, const char *itemId, const char *parentIdOrNull, int32_t seq) {
    return guard([&] {
        actorOk(actor);
        expect(project(db).items.setParent(str(itemId, "itemId"), optStr(parentIdOrNull), seq), project(db).failure());
    });
}

KiottySpecStatus KiottySpec_Item_remove(KiottySpec *db, KiottySpecActor actor, const char *itemId, const char *reason) {
    (void)reason;
    return guard([&] {
        actorOk(actor);
        project(db).removeItem(str(itemId, "itemId"));
    });
}

KiottySpecStatus KiottySpec_Item_exists(KiottySpec *db, const char *itemId, bool *outExists) {
    return guard([&] { *out(outExists) = expect(project(db).items.find(str(itemId, "itemId")), project(db).failure()).has_value(); });
}

KiottySpecStatus KiottySpec_Item_listBegin(KiottySpec *db, const KiottySpecItemFilter *filter, KiottySpecCursor **outCursor) {
    return guard([&] {
        out(outCursor);
        ItemQuery query;
        if (filter != nullptr) {
            query.kind = filter->kind;
            query.category = filter->category;
            query.parentId = filter->parentId != nullptr ? std::optional<std::string>(filter->parentId) : std::nullopt;
            query.text = optStr(filter->text);
            if (filter->atCheckpoint != nullptr) query.cut = cpSeq(db, filter->atCheckpoint);
        }
        cursorOf(outCursor, expect(project(db).items.list(query), project(db).failure()));
    });
}

KiottySpecStatus KiottySpec_Item_childrenBegin(KiottySpec *db, const char *itemId, bool recursive, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), expect(project(db).finder.children(str(itemId, "itemId"), recursive), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Item_searchBegin(KiottySpec *db, const char *query, size_t limit, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), expect(project(db).finder.search(str(query, "query"), limit), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Item_template(KiottySpecItemKind kind, KiottySpecItemCategory category, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = duplicateString(bodyTemplate(kind, category)); });
}

KiottySpecStatus KiottySpec_Milestone_setEntryState(KiottySpec *db, KiottySpecActor actor, const char *milestoneId, const char *itemId, KiottySpecImplementationState state) {
    return guard([&] {
        actorOk(actor);
        expect(project(db).milestones.setEntryState(str(milestoneId, "milestoneId"), str(itemId, "itemId"), state), project(db).failure());
    });
}

KiottySpecStatus KiottySpec_Milestone_entriesBegin(KiottySpec *db, const char *milestoneId, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), expect(project(db).milestones.entries(str(milestoneId, "milestoneId")), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Milestone_uncoveredBegin(KiottySpec *db, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), expect(project(db).milestones.uncovered(), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Milestone_next(KiottySpec *db, char **outMilestoneId) {
    return guard([&] {
        out(outMilestoneId);
        const auto next = expect(project(db).milestones.next(), project(db).failure());
        require(next.has_value(), KIOTTY_SPEC_STATUS_NOT_FOUND, "남은 마일스톤 없음");
        *outMilestoneId = duplicateString(*next);
    });
}

KiottySpecStatus KiottySpec_Milestone_progress(KiottySpec *db, size_t *outTotal, size_t *outImplemented, size_t *outNeedsChange) {
    return guard([&] {
        const auto progress = expect(project(db).milestones.progress(), project(db).failure());
        *out(outTotal) = progress.total;
        *out(outImplemented) = progress.implemented;
        *out(outNeedsChange) = progress.needsChange;
    });
}

KiottySpecStatus KiottySpec_File_register(KiottySpec *db, KiottySpecActor actor, KiottySpecFileRole role, const char *path, char **outId) {
    return guard([&] {
        actorOk(actor);
        require(role == KIOTTY_SPEC_FILE_ROLE_GUI_DRAFT || role == KIOTTY_SPEC_FILE_ROLE_C_HEADER || role == KIOTTY_SPEC_FILE_ROLE_SOURCE,
                KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "알 수 없는 파일 역할");
        *out(outId) = duplicateString(expect(project(db).files.registerFile(role, str(path, "path")), project(db).failure()));
    });
}

KiottySpecStatus KiottySpec_File_get(KiottySpec *db, const char *fileId, KiottySpecFile **outFile) {
    return guard([&] { *out(outFile) = packFile(expect(project(db).files.get(str(fileId, "fileId")), project(db).failure())); });
}

KiottySpecStatus KiottySpec_File_byPath(KiottySpec *db, const char *path, KiottySpecFile **outFile) {
    return guard([&] {
        const auto file = expect(project(db).files.byPath(str(path, "path")), project(db).failure());
        require(file.has_value(), KIOTTY_SPEC_STATUS_NOT_FOUND, "등록되지 않은 경로: " + std::string(path));
        *out(outFile) = packFile(*file);
    });
}

KiottySpecStatus KiottySpec_File_rescan(KiottySpec *db, KiottySpecActor actor, const char *fileId, bool *outChanged) {
    return guard([&] {
        actorOk(actor);
        const bool changed = expect(project(db).files.rescan(str(fileId, "fileId")), project(db).failure());
        if (outChanged != nullptr) *outChanged = changed;
    });
}

KiottySpecStatus KiottySpec_File_listBegin(KiottySpec *db, KiottySpecFileRole roleOr0, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), expect(project(db).files.list(roleOr0), project(db).failure())); });
}

KiottySpecStatus KiottySpec_File_changedSinceBegin(KiottySpec *db, const char *cpId, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), expect(project(db).files.changedSince(cpSeq(db, cpId)), project(db).failure())); });
}

KiottySpecStatus KiottySpec_File_scanGuiDraft(KiottySpec *db, const char *fileId, KiottySpecCursor **outUnknownIds, KiottySpecCursor **outMissingViews) {
    return guard([&] {
        out(outUnknownIds);
        out(outMissingViews);
        auto result = expect(project(db).graph.scanGuiDraft(str(fileId, "fileId")), project(db).failure());
        cursorOf(outUnknownIds, std::move(result.unknownIds));
        cursorOf(outMissingViews, std::move(result.missingViews));
    });
}

KiottySpecStatus KiottySpec_Link_add(KiottySpec *db, KiottySpecActor actor, const char *fromId, const char *toId, KiottySpecLinkType type) {
    return guard([&] {
        actorOk(actor);
        expect(project(db).links.add(str(fromId, "fromId"), str(toId, "toId"), type), project(db).failure());
    });
}

KiottySpecStatus KiottySpec_Link_remove(KiottySpec *db, KiottySpecActor actor, const char *fromId, const char *toId, KiottySpecLinkType type) {
    return guard([&] {
        actorOk(actor);
        expect(project(db).links.remove(str(fromId, "fromId"), str(toId, "toId"), type), project(db).failure());
    });
}

KiottySpecStatus KiottySpec_Link_listBegin(KiottySpec *db, const char *id, KiottySpecDirection dir, KiottySpecLinkType typeOr0, KiottySpecCursor **outCursor) {
    return guard([&] {
        require(dir >= KIOTTY_SPEC_DIRECTION_UPSTREAM && dir <= KIOTTY_SPEC_DIRECTION_BOTH, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "알 수 없는 방향");
        cursorOf(out(outCursor), expect(project(db).links.list(str(id, "id"), dir, typeOr0), project(db).failure()));
    });
}

KiottySpecStatus KiottySpec_Link_upstreamBegin(KiottySpec *db, const char *id, int32_t depth, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), expect(project(db).graph.traverse(str(id, "id"), KIOTTY_SPEC_DIRECTION_UPSTREAM, depth), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Link_downstreamBegin(KiottySpec *db, const char *id, int32_t depth, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), expect(project(db).graph.traverse(str(id, "id"), KIOTTY_SPEC_DIRECTION_DOWNSTREAM, depth), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Link_impactBegin(KiottySpec *db, const char *sinceCpId, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), expect(project(db).graph.impactSince(cpSeq(db, sinceCpId)), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Link_scopeOf(KiottySpec *db, const char *designOrFileId, KiottySpecScope *outScope, KiottySpecCursor **outAffectedModules) {
    return guard([&] {
        auto &p = project(db);
        auto result = expect(p.graph.scopeOf(str(designOrFileId, "id")), p.failure());
        *out(outScope) = result.scope;
        if (outAffectedModules != nullptr) {
            std::vector<ItemRecord> modules;
            for (const auto &id : result.affectedModules) {
                if (auto item = expect(p.items.find(id), p.failure())) modules.push_back(std::move(*item));
            }
            cursorOf(outAffectedModules, std::move(modules));
        }
    });
}

KiottySpecStatus KiottySpec_Link_matrix(KiottySpec *db, KiottySpecItemKind rowKind, KiottySpecItemKind colKind, char **outCsv) {
    return guard([&] { *out(outCsv) = duplicateString(expect(project(db).graph.matrix(rowKind, colKind), project(db).failure())); });
}

KiottySpecStatus KiottySpec_ChangeRequest_create(KiottySpec *db, KiottySpecActor actor, KiottySpecChangeSource source, const char *body, int32_t entryStep, char **outId) {
    return guard([&] {
        actorOk(actor);
        *out(outId) = duplicateString(expect(project(db).changes.create(source, str(body, "body"), entryStep), project(db).failure()));
    });
}

KiottySpecStatus KiottySpec_ChangeRequest_get(KiottySpec *db, const char *crId, KiottySpecChangeRequest **outCr) {
    return guard([&] { *out(outCr) = packChangeRequest(expect(project(db).changes.get(str(crId, "crId")), project(db).failure())); });
}

KiottySpecStatus KiottySpec_ChangeRequest_setState(KiottySpec *db, KiottySpecActor actor, const char *crId, KiottySpecChangeState state, const char *note) {
    return guard([&] {
        actorOk(actor);
        require(state >= KIOTTY_SPEC_CHANGE_STATE_OPEN && state <= KIOTTY_SPEC_CHANGE_STATE_REJECTED, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "알 수 없는 상태");
        expect(project(db).changes.setState(str(crId, "crId"), state, note != nullptr ? note : ""), project(db).failure());
    });
}

KiottySpecStatus KiottySpec_ChangeRequest_setEntryStep(KiottySpec *db, KiottySpecActor actor, const char *crId, int32_t entryStep) {
    return guard([&] {
        actorOk(actor);
        expect(project(db).changes.setEntryStep(str(crId, "crId"), entryStep), project(db).failure());
    });
}

KiottySpecStatus KiottySpec_ChangeRequest_listBegin(KiottySpec *db, KiottySpecChangeState stateOr0, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), expect(project(db).changes.list(stateOr0), project(db).failure())); });
}

KiottySpecStatus KiottySpec_ChangeRequest_openCount(KiottySpec *db, size_t *outCount) {
    return guard([&] { *out(outCount) = expect(project(db).changes.openCount(), project(db).failure()); });
}

KiottySpecStatus KiottySpec_Decision_get(KiottySpec *db, KiottySpecDecisionKind kind, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = duplicateString(expect(project(db).decisions.get(kind), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Decision_rewrite(KiottySpec *db, KiottySpecActor actor, KiottySpecDecisionKind kind, const char *markdown) {
    return guard([&] {
        actorOk(actor);
        expect(project(db).decisions.rewrite(kind, str(markdown, "markdown")), project(db).failure());
    });
}

KiottySpecStatus KiottySpec_Decision_check(KiottySpec *db, KiottySpecDecisionKind kind, char **outReport) {
    return guard([&] { *out(outReport) = duplicateString(expect(project(db).decisions.check(kind), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Decision_getAt(KiottySpec *db, KiottySpecDecisionKind kind, const char *cpId, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = duplicateString(expect(project(db).decisions.get(kind, cpSeq(db, cpId)), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Result_record(KiottySpec *db, KiottySpecActor actor, const char *targetId, KiottySpecTestResult result, const char *log) {
    return guard([&] {
        actorOk(actor);
        expect(project(db).results.record(str(targetId, "targetId"), result, log != nullptr ? log : ""), project(db).failure());
    });
}

KiottySpecStatus KiottySpec_Result_last(KiottySpec *db, const char *targetId, KiottySpecResult **outResult) {
    return guard([&] {
        out(outResult);
        const auto result = expect(project(db).results.last(str(targetId, "targetId")), project(db).failure());
        require(result.has_value(), KIOTTY_SPEC_STATUS_NOT_FOUND, "결과 없음: " + std::string(targetId));
        *outResult = packResult(*result);
    });
}

KiottySpecStatus KiottySpec_Result_listBegin(KiottySpec *db, const char *targetId, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), expect(project(db).results.history(str(targetId, "targetId")), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Result_failedBegin(KiottySpec *db, KiottySpecItemKind kindOr0, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), expect(project(db).results.failed(kindOr0), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Result_summary(KiottySpec *db, KiottySpecItemKind kind, size_t *outTotal, size_t *outPass, size_t *outFail, size_t *outNotRun) {
    return guard([&] {
        const auto summary = expect(project(db).results.summary(kind), project(db).failure());
        *out(outTotal) = summary.total;
        *out(outPass) = summary.pass;
        *out(outFail) = summary.fail;
        *out(outNotRun) = summary.notRun;
    });
}

KiottySpecStatus KiottySpec_Result_suspectsBegin(KiottySpec *db, const char *failedTargetId, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), expect(project(db).graph.suspects(str(failedTargetId, "failedTargetId")), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Check_requirementsWithoutCriteriaBegin(KiottySpec *db, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), expect(project(db).checker.requirementsWithoutCriteria(), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Check_systemTestOneToOne(KiottySpec *db, KiottySpecCursor **outMissing, KiottySpecCursor **outDuplicated) {
    return guard([&] {
        auto result = expect(project(db).checker.oneToOne(KIOTTY_SPEC_ITEM_KIND_SYSTEM_TEST), project(db).failure());
        cursorOf(out(outMissing), std::move(result.missing));
        cursorOf(out(outDuplicated), std::move(result.duplicated));
    });
}

KiottySpecStatus KiottySpec_Check_e2eTestOneToOne(KiottySpec *db, KiottySpecCursor **outMissing, KiottySpecCursor **outDuplicated) {
    return guard([&] {
        auto result = expect(project(db).checker.oneToOne(KIOTTY_SPEC_ITEM_KIND_END_TO_END_TEST), project(db).failure());
        cursorOf(out(outMissing), std::move(result.missing));
        cursorOf(out(outDuplicated), std::move(result.duplicated));
    });
}

KiottySpecStatus KiottySpec_Check_unlinkedBegin(KiottySpec *db, KiottySpecItemKind kind, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), expect(project(db).checker.unlinked(kind), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Check_orphanTestsBegin(KiottySpec *db, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), expect(project(db).checker.orphanTests(), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Check_all(KiottySpec *db, char **outReport) {
    return guard([&] { *out(outReport) = duplicateString(expect(project(db).checker.all(), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Context_forStep(KiottySpec *db, int32_t step, size_t maxBytes, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = duplicateString(project(db).context.forStep(step, maxBytes)); });
}

KiottySpecStatus KiottySpec_Context_forItem(KiottySpec *db, const char *itemId, int32_t depth, size_t maxBytes, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = duplicateString(project(db).context.forItem(str(itemId, "itemId"), depth, maxBytes)); });
}

KiottySpecStatus KiottySpec_Context_forMilestone(KiottySpec *db, const char *milestoneId, size_t maxBytes, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = duplicateString(project(db).context.forMilestone(str(milestoneId, "milestoneId"), maxBytes)); });
}

KiottySpecStatus KiottySpec_Context_forChangeRequest(KiottySpec *db, const char *crId, size_t maxBytes, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = duplicateString(project(db).context.forChangeRequest(str(crId, "crId"), maxBytes)); });
}

KiottySpecStatus KiottySpec_Context_rules(KiottySpec *db, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = duplicateString(project(db).context.rules()); });
}

KiottySpecStatus KiottySpec_Context_pending(KiottySpec *db, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = duplicateString(project(db).context.pending()); });
}

KiottySpecStatus KiottySpec_Context_whereAmI(KiottySpec *db, size_t maxBytes, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = duplicateString(expect(project(db).journey.whereAmI(maxBytes), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Context_status(KiottySpec *db, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = duplicateString(project(db).context.status()); });
}

KiottySpecStatus KiottySpec_Export_markdown(KiottySpec *db, KiottySpecItemKind kind, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = duplicateString(exportMarkdown(project(db), kind)); });
}

KiottySpecStatus KiottySpec_Export_traceReport(KiottySpec *db, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = duplicateString(expect(project(db).exchange.traceReport(), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Export_jsonl(KiottySpec *db, const char *dir) {
    return guard([&] {
        auto &p = project(db);
        const SystemPath target = jsonlTarget(p, dir);
        expect(p.exchange.exportJsonl(target.view()), p.failure());
    });
}

KiottySpecStatus KiottySpec_Import_jsonl(KiottySpec *db, const char *dir, bool merge, char **outConflictReport) {
    KiottySpecStatus conflictStatus = KIOTTY_SPEC_STATUS_SUCCESS;
    const KiottySpecStatus status = guard([&] {
        auto &p = project(db);
        const SystemPath target = jsonlTarget(p, dir);
        const auto report = expect(p.exchange.importJsonl(target.view(), merge), p.failure());
        std::string text = std::to_string(report.rows) + " rows\n";
        for (const auto &conflict : report.conflicts) text += "conflict " + conflict + "\n";
        if (outConflictReport != nullptr) *outConflictReport = duplicateString(text);
        if (!report.conflicts.empty()) conflictStatus = KIOTTY_SPEC_STATUS_CONFLICT;
    });
    return status != KIOTTY_SPEC_STATUS_SUCCESS ? status : conflictStatus;
}

KiottySpecStatus KiottySpec_Import_markdown(KiottySpec *db, KiottySpecActor actor, KiottySpecItemKind kind, const char *markdown, KiottySpecCursor **outCreatedIds) {
    return guard([&] {
        actorOk(actor);
        auto ids = expect(project(db).exchange.importMarkdown(kind, str(markdown, "markdown")), project(db).failure());
        if (outCreatedIds != nullptr) cursorOf(outCreatedIds, std::move(ids));
    });
}

KiottySpecStatus KiottySpec_Query_readonly(KiottySpec *db, const char *sql, char **outJson) {
    return guard([&] { *out(outJson) = duplicateString(expect(project(db).exchange.queryReadonly(str(sql, "sql")), project(db).failure())); });
}

KiottySpecStatus KiottySpec_Schema_version(KiottySpec *db, int32_t *outVersion) {
    return guard([&] { *out(outVersion) = expect(kiotty::spec::implementation::version(project(db).storage()), project(db).failure()); });
}

KiottySpecStatus KiottySpec_Schema_migrate(KiottySpec *db) {
    return guard([&] { expect(kiotty::spec::implementation::ensure(project(db).storage()), project(db).failure()); });
}

}
