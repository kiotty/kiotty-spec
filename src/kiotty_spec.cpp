// include/kiotty_spec.h 의 C 인터페이스. 인자 검사와 예외→상태 코드 변환만 하고, 일은 implementation/base 에 맡긴다.

#include "kiotty_spec.h"

#include "implementation/base/project.h"
#include "implementation/base/schema.h"
#include "implementation/core/cursor.h"
#include "implementation/core/error.h"
#include "implementation/core/names.h"
#include "implementation/core/text.h"

#include <algorithm>
#include <cstdlib>
#include <new>
#include <set>
#include <string>
#include <type_traits>
#include <variant>

struct KiottySpec {
    explicit KiottySpec(const std::filesystem::path &root) : project(root) {}
    kiotty::base::Project project;
};

namespace {

using kiotty::core::Error;
using kiotty::core::require;
namespace core = kiotty::core;
namespace base = kiotty::base;

thread_local std::string g_lastError;

template <class Body>
KiottySpecStatus guard(Body &&body) {
    try {
        body();
        g_lastError.clear();
        return KiottySpecStatus_Ok;
    } catch (const Error &e) {
        g_lastError = e.what();
        return e.status();
    } catch (const std::bad_alloc &) {
        g_lastError = "메모리 부족";
        return KiottySpecStatus_Db;
    } catch (const std::exception &e) {
        g_lastError = e.what();
        return KiottySpecStatus_Db;
    } catch (...) {
        g_lastError = "알 수 없는 오류";
        return KiottySpecStatus_Db;
    }
}

base::Project &project(KiottySpec *db) {
    require(db != nullptr, KiottySpecStatus_InvalidArg, "db 핸들이 NULL");
    return db->project;
}

std::string str(const char *text, const char *what) {
    require(text != nullptr, KiottySpecStatus_InvalidArg, std::string(what) + " 이 NULL");
    return text;
}

std::optional<std::string> optStr(const char *text) {
    if (text == nullptr || *text == '\0') return std::nullopt;
    return std::string(text);
}

template <class T>
T *out(T *pointer) {
    require(pointer != nullptr, KiottySpecStatus_InvalidArg, "out 인자가 NULL");
    return pointer;
}

void actorOk(KiottySpecActor actor) {
    require(core::isValidActor(actor), KiottySpecStatus_InvalidArg, "actor 는 User 또는 Ai");
}

int64_t cpSeq(KiottySpec *db, const char *cpId) { return project(db).checkpoints.resolve(str(cpId, "체크포인트 ID")); }

template <class Records>
KiottySpecStatus cursorOf(KiottySpecCursor **outCursor, Records &&records) {
    *outCursor = new KiottySpecCursor{core::boxAll(std::forward<Records>(records))};
    return KiottySpecStatus_Ok;
}

template <class Box, class View>
KiottySpecStatus cursorGet(KiottySpecCursor *cursor, size_t index, const View **outView) {
    return guard([&] {
        require(cursor != nullptr, KiottySpecStatus_InvalidArg, "cursor 가 NULL");
        out(outView);
        auto *list = std::get_if<core::BoxList<Box>>(&cursor->payload);
        require(list != nullptr, KiottySpecStatus_WrongCursorType, "커서 원소 종류가 다름");
        require(index < list->size(), KiottySpecStatus_InvalidArg, "index 범위 초과");
        *outView = &(*list)[index]->view;
    });
}


template <class Enum>
const char *enumNameOf(int value) {
    return core::name(static_cast<Enum>(value)).data();
}

template <class Enum>
bool enumParseOf(std::string_view text, int &out) {
    Enum value{};
    if (!core::parse(text, value)) return false;
    out = static_cast<int>(value);
    return true;
}

const char *directionName(int value) {
    switch (value) {
    case KiottySpecDirection_Upstream: return "up";
    case KiottySpecDirection_Downstream: return "down";
    case KiottySpecDirection_Both: return "both";
    default: return "unknown";
    }
}

const char *actorName(int value) {
    switch (value) {
    case KiottySpecActor_User: return "user";
    case KiottySpecActor_Ai: return "ai";
    default: return "unknown";
    }
}

} // namespace

extern "C" {

/* ---------------------------------------------------------------- 4. 수명 주기 */

KiottySpecStatus KiottySpec_open(const char *projectRoot, KiottySpec **outDb) {
    return guard([&] {
        out(outDb);
        *outDb = nullptr;
        *outDb = new KiottySpec(core::pathFromUtf8(str(projectRoot, "projectRoot")));
    });
}

KiottySpecStatus KiottySpec_close(KiottySpec *db) {
    return guard([&] { delete db; });
}

KiottySpecStatus KiottySpec_init(KiottySpec *db, const char *projectName, bool installGitHooks) {
    return guard([&] { project(db).init(projectName != nullptr ? projectName : "", installGitHooks); });
}

KiottySpecStatus KiottySpec_setPolicy(KiottySpec *db, const char *key, const char *value) {
    return guard([&] { project(db).meta.setPolicy(str(key, "key"), str(value, "value")); });
}

KiottySpecStatus KiottySpec_getPolicy(KiottySpec *db, const char *key, char **outValue) {
    return guard([&] {
        out(outValue);
        const auto value = project(db).meta.policy(str(key, "key"));
        require(value.has_value(), KiottySpecStatus_NotFound, "정책 없음: " + std::string(key));
        *outValue = core::duplicateString(*value);
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

const char *KiottySpec_strerror(KiottySpecStatus status) { return core::name(status).data(); }

const char *KiottySpec_lastError(void) { return g_lastError.c_str(); }


const char *KiottySpec_enumName(const char *enumType, int value) {
    const std::string_view type = enumType != nullptr ? enumType : "";
    if (type == "ItemKind") return enumNameOf<KiottySpecItemKind>(value);
    if (type == "ItemCategory") return enumNameOf<KiottySpecItemCategory>(value);
    if (type == "FileRole") return enumNameOf<KiottySpecFileRole>(value);
    if (type == "LinkType") return enumNameOf<KiottySpecLinkType>(value);
    if (type == "ChangeSource") return enumNameOf<KiottySpecChangeSource>(value);
    if (type == "ChangeState") return enumNameOf<KiottySpecChangeState>(value);
    if (type == "ImplState") return enumNameOf<KiottySpecImplState>(value);
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
        else if (type == "ImplState") ok = enumParseOf<KiottySpecImplState>(text, value);
        else if (type == "TestResult") ok = enumParseOf<KiottySpecTestResult>(text, value);
        else if (type == "DecisionKind") ok = enumParseOf<KiottySpecDecisionKind>(text, value);
        else if (type == "CheckpointPurpose") ok = enumParseOf<KiottySpecCheckpointPurpose>(text, value);
        else if (type == "Scope") {
            ok = text == "intra_module" || text == "inter_module";
            value = text == "intra_module" ? KiottySpecScope_IntraModule : KiottySpecScope_InterModule;
        } else if (type == "Direction") {
            for (int candidate = KiottySpecDirection_Upstream; candidate <= KiottySpecDirection_Both; ++candidate) {
                if (text == directionName(candidate)) { value = candidate; ok = true; }
            }
        } else if (type == "Actor") {
            for (int candidate = KiottySpecActor_User; candidate <= KiottySpecActor_Ai; ++candidate) {
                if (text == actorName(candidate)) { value = candidate; ok = true; }
            }
        } else {
            kiotty::core::fail(KiottySpecStatus_InvalidArg, "알 수 없는 enumType: " + type);
        }
        require(ok, KiottySpecStatus_InvalidArg, "알 수 없는 " + type + " 이름: '" + text + "'");
        *out(outValue) = value;
    });
}

/* ---------------------------------------------------------------- 5. 커서 */

KiottySpecStatus KiottySpec_Cursor_count(KiottySpecCursor *cursor, size_t *outCount) {
    return guard([&] {
        require(cursor != nullptr, KiottySpecStatus_InvalidArg, "cursor 가 NULL");
        *out(outCount) = std::visit([](const auto &list) { return list.size(); }, cursor->payload);
    });
}

KiottySpecStatus KiottySpec_Cursor_getItem(KiottySpecCursor *c, size_t i, const KiottySpecItem **o) { return cursorGet<core::ItemBox>(c, i, o); }
KiottySpecStatus KiottySpec_Cursor_getLink(KiottySpecCursor *c, size_t i, const KiottySpecLink **o) { return cursorGet<core::LinkBox>(c, i, o); }
KiottySpecStatus KiottySpec_Cursor_getFile(KiottySpecCursor *c, size_t i, const KiottySpecFile **o) { return cursorGet<core::FileBox>(c, i, o); }
KiottySpecStatus KiottySpec_Cursor_getCheckpoint(KiottySpecCursor *c, size_t i, const KiottySpecCheckpoint **o) { return cursorGet<core::CheckpointBox>(c, i, o); }
KiottySpecStatus KiottySpec_Cursor_getChangeRequest(KiottySpecCursor *c, size_t i, const KiottySpecChangeRequest **o) { return cursorGet<core::ChangeRequestBox>(c, i, o); }
KiottySpecStatus KiottySpec_Cursor_getResult(KiottySpecCursor *c, size_t i, const KiottySpecResult **o) { return cursorGet<core::ResultBox>(c, i, o); }
KiottySpecStatus KiottySpec_Cursor_getMilestoneEntry(KiottySpecCursor *c, size_t i, const KiottySpecMilestoneEntry **o) { return cursorGet<core::MilestoneEntryBox>(c, i, o); }
KiottySpecStatus KiottySpec_Cursor_getStepRun(KiottySpecCursor *c, size_t i, const KiottySpecStepRun **o) { return cursorGet<core::StepRunBox>(c, i, o); }

KiottySpecStatus KiottySpec_Cursor_getId(KiottySpecCursor *cursor, size_t index, const char **outId) {
    return guard([&] {
        require(cursor != nullptr, KiottySpecStatus_InvalidArg, "cursor 가 NULL");
        out(outId);
        std::visit(
            [&](const auto &list) {
                require(index < list.size(), KiottySpecStatus_InvalidArg, "index 범위 초과");
                using Box = typename std::decay_t<decltype(list)>::value_type::element_type;
                const auto &box = *list[index];
                if constexpr (std::is_same_v<Box, core::IdBox>) *outId = box.id.c_str();
                else if constexpr (std::is_same_v<Box, core::LinkBox>) *outId = box.view.fromId;
                else if constexpr (std::is_same_v<Box, core::ResultBox>) *outId = box.view.targetId;
                else if constexpr (std::is_same_v<Box, core::MilestoneEntryBox>) *outId = box.view.itemId;
                else if constexpr (std::is_same_v<Box, core::StepRunBox>) *outId = box.view.lastCheckpointId;
                else *outId = box.view.id;
            },
            cursor->payload);
    });
}

void KiottySpec_Cursor_end(KiottySpecCursor *cursor) { delete cursor; }

/* ---------------------------------------------------------------- 6. 체크포인트 */

KiottySpecStatus KiottySpec_Checkpoint_create(KiottySpec *db, KiottySpecActor actor, int32_t step, const char *note, char **outId) {
    return guard([&] {
        actorOk(actor);
        out(outId);
        const int64_t seq = project(db).checkpoints.create(step, note != nullptr ? note : "", KiottySpecCheckpointPurpose_None);
        *outId = core::duplicateString(core::checkpointName(seq));
    });
}

KiottySpecStatus KiottySpec_Checkpoint_confirm(KiottySpec *db, KiottySpecActor actor, const char *cpId) {
    return guard([&] { project(db).confirm(actor, str(cpId, "cpId")); });
}

KiottySpecStatus KiottySpec_Checkpoint_get(KiottySpec *db, const char *cpId, KiottySpecCheckpoint **outCp) {
    return guard([&] {
        out(outCp);
        const int64_t seq = cpSeq(db, cpId);
        require(seq != core::kPending, KiottySpecStatus_InvalidArg, "PENDING 은 체크포인트가 아님");
        *outCp = core::packCheckpoint(project(db).checkpoints.get(seq));
    });
}

KiottySpecStatus KiottySpec_Checkpoint_current(KiottySpec *db, KiottySpecCheckpoint **outCp) {
    return guard([&] {
        out(outCp);
        const auto latest = project(db).checkpoints.latest();
        require(latest.has_value(), KiottySpecStatus_NotFound, "체크포인트가 아직 없음");
        *outCp = core::packCheckpoint(*latest);
    });
}

KiottySpecStatus KiottySpec_Checkpoint_lastConfirmed(KiottySpec *db, int32_t stepOr0, KiottySpecCheckpoint **outCp) {
    return guard([&] {
        out(outCp);
        const auto record = project(db).checkpoints.lastConfirmed(stepOr0);
        require(record.has_value(), KiottySpecStatus_NotFound, "확정된 체크포인트 없음");
        *outCp = core::packCheckpoint(*record);
    });
}

KiottySpecStatus KiottySpec_Checkpoint_listBegin(KiottySpec *db, int32_t stepOr0, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), project(db).checkpoints.list(stepOr0)); });
}

KiottySpecStatus KiottySpec_Checkpoint_changedItemsBegin(KiottySpec *db, const char *cpFrom, const char *cpToOrNull, KiottySpecCursor **outCursor) {
    return guard([&] {
        out(outCursor);
        const int64_t from = cpSeq(db, cpFrom);
        const int64_t to = cpToOrNull != nullptr ? cpSeq(db, cpToOrNull) : core::kCurrent;
        std::vector<core::ItemRecord> records;
        for (auto &change : project(db).items.changedBetween(from, to)) {
            if (change.record.removed) change.record.title = "[삭제됨] " + change.record.title;
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
        std::string md = "# " + core::checkpointName(a) + " → " + core::checkpointName(b) + "\n\n## 항목\n";
        const auto changes = p.items.changedBetween(std::min(a, b), std::max(a, b));
        if (changes.empty()) md += "변경 없음\n";
        for (const auto &change : changes) {
            const char *what = change.record.removed ? "삭제" : change.added ? "추가" : "수정";
            md += "- " + std::string(what) + " " + change.record.id + " " + change.record.title + "\n";
        }
        md += "\n## 링크\n";
        const auto before = p.links.all(std::min(a, b));
        const auto after = p.links.all(std::max(a, b));
        const auto key = [](const core::LinkRecord &l) { return l.fromId + " " + std::string(core::name(l.type)) + " " + l.toId; };
        std::set<std::string> beforeKeys, afterKeys;
        for (const auto &l : before) beforeKeys.insert(key(l));
        for (const auto &l : after) afterKeys.insert(key(l));
        for (const auto &k : afterKeys) if (beforeKeys.count(k) == 0) md += "- 추가 " + k + "\n";
        for (const auto &k : beforeKeys) if (afterKeys.count(k) == 0) md += "- 삭제 " + k + "\n";
        md += "\n## 결정 기록\n";
        for (const auto kind : {KiottySpecDecisionKind_Requirement, KiottySpecDecisionKind_Rule, KiottySpecDecisionKind_Interface,
                                KiottySpecDecisionKind_Design, KiottySpecDecisionKind_UnitTest, KiottySpecDecisionKind_Plan}) {
            if (p.decisions.get(kind, std::min(a, b)) != p.decisions.get(kind, std::max(a, b))) {
                md += "- 바뀜: " + std::string(core::name(kind)) + "\n";
            }
        }
        *outMarkdown = core::duplicateString(md);
    });
}

KiottySpecStatus KiottySpec_Checkpoint_confirmCheck(KiottySpec *db, int32_t step, bool *outOk, char **outReport) {
    return guard([&] {
        const auto verdict = project(db).checker.confirmCheck(step);
        *out(outOk) = verdict.ok;
        *out(outReport) = core::duplicateString(verdict.report);
    });
}

KiottySpecStatus KiottySpec_Checkpoint_releaseCheck(KiottySpec *db, bool *outOk, char **outReport) {
    return guard([&] {
        const auto verdict = project(db).checker.releaseCheck();
        *out(outOk) = verdict.ok;
        *out(outReport) = core::duplicateString(verdict.report);
    });
}

KiottySpecStatus KiottySpec_Checkpoint_position(KiottySpec *db, KiottySpecPosition **outPosition) {
    return guard([&] { *out(outPosition) = core::packPosition(project(db).journey.position()); });
}

KiottySpecStatus KiottySpec_Checkpoint_journeyBegin(KiottySpec *db, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), project(db).journey.runs()); });
}

KiottySpecStatus KiottySpec_Checkpoint_release(KiottySpec *db, KiottySpecActor actor, const char *note, char **outId) {
    return guard([&] {
        out(outId);
        *outId = core::duplicateString(core::checkpointName(project(db).release(actor, note != nullptr ? note : "")));
    });
}

KiottySpecStatus KiottySpec_Checkpoint_prune(KiottySpec *db, KiottySpecActor actor, const char *baseCpId, size_t *outRemoved) {
    return guard([&] {
        const size_t removed = project(db).prune(actor, str(baseCpId, "baseCpId"));
        if (outRemoved != nullptr) *outRemoved = removed;
    });
}

/* ---------------------------------------------------------------- 7. 항목 */

KiottySpecStatus KiottySpec_Item_create(KiottySpec *db, KiottySpecActor actor, KiottySpecItemKind kind, KiottySpecItemCategory category,
                                        const char *parentIdOrNull, const char *title, const char *body, char **outId) {
    return guard([&] {
        actorOk(actor);
        out(outId);
        *outId = core::duplicateString(
            project(db).items.create(kind, category, optStr(parentIdOrNull), str(title, "title"), body != nullptr ? body : ""));
    });
}

KiottySpecStatus KiottySpec_Item_get(KiottySpec *db, const char *itemId, KiottySpecItem **outItem) {
    return guard([&] { *out(outItem) = core::packItem(project(db).items.get(str(itemId, "itemId"))); });
}

KiottySpecStatus KiottySpec_Item_getAt(KiottySpec *db, const char *itemId, const char *cpId, KiottySpecItem **outItem) {
    return guard([&] { *out(outItem) = core::packItem(project(db).items.get(str(itemId, "itemId"), cpSeq(db, cpId))); });
}

KiottySpecStatus KiottySpec_Item_update(KiottySpec *db, KiottySpecActor actor, const char *itemId, const char *title, const char *body) {
    return guard([&] {
        actorOk(actor);
        project(db).items.update(str(itemId, "itemId"), title != nullptr ? std::optional<std::string>(title) : std::nullopt,
                                 body != nullptr ? std::optional<std::string>(body) : std::nullopt);
    });
}

KiottySpecStatus KiottySpec_Item_setParent(KiottySpec *db, KiottySpecActor actor, const char *itemId, const char *parentIdOrNull, int32_t seq) {
    return guard([&] {
        actorOk(actor);
        project(db).items.setParent(str(itemId, "itemId"), optStr(parentIdOrNull), seq);
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
    return guard([&] { *out(outExists) = project(db).items.find(str(itemId, "itemId")).has_value(); });
}

KiottySpecStatus KiottySpec_Item_listBegin(KiottySpec *db, const KiottySpecItemFilter *filter, KiottySpecCursor **outCursor) {
    return guard([&] {
        out(outCursor);
        base::ItemQuery query;
        if (filter != nullptr) {
            query.kind = filter->kind;
            query.category = filter->category;
            query.parentId = filter->parentId != nullptr ? std::optional<std::string>(filter->parentId) : std::nullopt;
            query.text = optStr(filter->text);
            if (filter->atCheckpoint != nullptr) query.cut = cpSeq(db, filter->atCheckpoint);
        }
        cursorOf(outCursor, project(db).items.list(query));
    });
}

KiottySpecStatus KiottySpec_Item_childrenBegin(KiottySpec *db, const char *itemId, bool recursive, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), project(db).items.children(str(itemId, "itemId"), recursive)); });
}

KiottySpecStatus KiottySpec_Item_searchBegin(KiottySpec *db, const char *query, size_t limit, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), project(db).items.search(str(query, "query"), limit)); });
}

KiottySpecStatus KiottySpec_Item_template(KiottySpecItemKind kind, KiottySpecItemCategory category, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = core::duplicateString(base::ItemStore::bodyTemplate(kind, category)); });
}

/* ---------------------------------------------------------------- 8. 마일스톤 */

KiottySpecStatus KiottySpec_Milestone_setEntryState(KiottySpec *db, KiottySpecActor actor, const char *milestoneId, const char *itemId, KiottySpecImplState state) {
    return guard([&] {
        actorOk(actor);
        project(db).milestones.setEntryState(str(milestoneId, "milestoneId"), str(itemId, "itemId"), state);
    });
}

KiottySpecStatus KiottySpec_Milestone_entriesBegin(KiottySpec *db, const char *milestoneId, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), project(db).milestones.entries(str(milestoneId, "milestoneId"))); });
}

KiottySpecStatus KiottySpec_Milestone_uncoveredBegin(KiottySpec *db, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), project(db).milestones.uncovered()); });
}

KiottySpecStatus KiottySpec_Milestone_next(KiottySpec *db, char **outMilestoneId) {
    return guard([&] {
        out(outMilestoneId);
        const auto next = project(db).milestones.next();
        require(next.has_value(), KiottySpecStatus_NotFound, "남은 마일스톤 없음");
        *outMilestoneId = core::duplicateString(*next);
    });
}

KiottySpecStatus KiottySpec_Milestone_progress(KiottySpec *db, size_t *outTotal, size_t *outImplemented, size_t *outNeedsChange) {
    return guard([&] {
        const auto progress = project(db).milestones.progress();
        *out(outTotal) = progress.total;
        *out(outImplemented) = progress.implemented;
        *out(outNeedsChange) = progress.needsChange;
    });
}

/* ---------------------------------------------------------------- 9. 파일 */

KiottySpecStatus KiottySpec_File_register(KiottySpec *db, KiottySpecActor actor, KiottySpecFileRole role, const char *path, char **outId) {
    return guard([&] {
        actorOk(actor);
        require(role == KiottySpecFileRole_GuiDraft || role == KiottySpecFileRole_CHeader || role == KiottySpecFileRole_Source,
                KiottySpecStatus_InvalidArg, "알 수 없는 파일 역할");
        *out(outId) = core::duplicateString(project(db).files.registerFile(role, str(path, "path")));
    });
}

KiottySpecStatus KiottySpec_File_get(KiottySpec *db, const char *fileId, KiottySpecFile **outFile) {
    return guard([&] { *out(outFile) = core::packFile(project(db).files.get(str(fileId, "fileId"))); });
}

KiottySpecStatus KiottySpec_File_byPath(KiottySpec *db, const char *path, KiottySpecFile **outFile) {
    return guard([&] {
        const auto file = project(db).files.byPath(str(path, "path"));
        require(file.has_value(), KiottySpecStatus_NotFound, "등록되지 않은 경로: " + std::string(path));
        *out(outFile) = core::packFile(*file);
    });
}

KiottySpecStatus KiottySpec_File_rescan(KiottySpec *db, KiottySpecActor actor, const char *fileId, bool *outChanged) {
    return guard([&] {
        actorOk(actor);
        const bool changed = project(db).files.rescan(str(fileId, "fileId"));
        if (outChanged != nullptr) *outChanged = changed;
    });
}

KiottySpecStatus KiottySpec_File_listBegin(KiottySpec *db, KiottySpecFileRole roleOr0, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), project(db).files.list(roleOr0)); });
}

KiottySpecStatus KiottySpec_File_changedSinceBegin(KiottySpec *db, const char *cpId, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), project(db).files.changedSince(cpSeq(db, cpId))); });
}

KiottySpecStatus KiottySpec_File_scanGuiDraft(KiottySpec *db, const char *fileId, KiottySpecCursor **outUnknownIds, KiottySpecCursor **outMissingViews) {
    return guard([&] {
        out(outUnknownIds);
        out(outMissingViews);
        auto result = project(db).links.scanGuiDraft(str(fileId, "fileId"));
        cursorOf(outUnknownIds, std::move(result.unknownIds));
        cursorOf(outMissingViews, std::move(result.missingViews));
    });
}

/* ---------------------------------------------------------------- 10. 링크 */

KiottySpecStatus KiottySpec_Link_add(KiottySpec *db, KiottySpecActor actor, const char *fromId, const char *toId, KiottySpecLinkType type) {
    return guard([&] {
        actorOk(actor);
        project(db).links.add(str(fromId, "fromId"), str(toId, "toId"), type);
    });
}

KiottySpecStatus KiottySpec_Link_remove(KiottySpec *db, KiottySpecActor actor, const char *fromId, const char *toId, KiottySpecLinkType type) {
    return guard([&] {
        actorOk(actor);
        project(db).links.remove(str(fromId, "fromId"), str(toId, "toId"), type);
    });
}

KiottySpecStatus KiottySpec_Link_listBegin(KiottySpec *db, const char *id, KiottySpecDirection dir, KiottySpecLinkType typeOr0, KiottySpecCursor **outCursor) {
    return guard([&] {
        require(dir >= KiottySpecDirection_Upstream && dir <= KiottySpecDirection_Both, KiottySpecStatus_InvalidArg, "알 수 없는 방향");
        cursorOf(out(outCursor), project(db).links.list(str(id, "id"), dir, typeOr0));
    });
}

KiottySpecStatus KiottySpec_Link_upstreamBegin(KiottySpec *db, const char *id, int32_t depth, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), project(db).links.traverse(str(id, "id"), KiottySpecDirection_Upstream, depth)); });
}

KiottySpecStatus KiottySpec_Link_downstreamBegin(KiottySpec *db, const char *id, int32_t depth, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), project(db).links.traverse(str(id, "id"), KiottySpecDirection_Downstream, depth)); });
}

KiottySpecStatus KiottySpec_Link_impactBegin(KiottySpec *db, const char *sinceCpId, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), project(db).links.impactSince(cpSeq(db, sinceCpId))); });
}

KiottySpecStatus KiottySpec_Link_scopeOf(KiottySpec *db, const char *designOrFileId, KiottySpecScope *outScope, KiottySpecCursor **outAffectedModules) {
    return guard([&] {
        auto &p = project(db);
        auto result = p.links.scopeOf(str(designOrFileId, "id"));
        *out(outScope) = result.scope;
        if (outAffectedModules != nullptr) {
            std::vector<core::ItemRecord> modules;
            for (const auto &id : result.affectedModules) {
                if (auto item = p.items.find(id)) modules.push_back(std::move(*item));
            }
            cursorOf(outAffectedModules, std::move(modules));
        }
    });
}

KiottySpecStatus KiottySpec_Link_matrix(KiottySpec *db, KiottySpecItemKind rowKind, KiottySpecItemKind colKind, char **outCsv) {
    return guard([&] { *out(outCsv) = core::duplicateString(project(db).links.matrix(rowKind, colKind)); });
}

/* ---------------------------------------------------------------- 11. 변경 요청 */

KiottySpecStatus KiottySpec_ChangeRequest_create(KiottySpec *db, KiottySpecActor actor, KiottySpecChangeSource source, const char *body, int32_t entryStep, char **outId) {
    return guard([&] {
        actorOk(actor);
        *out(outId) = core::duplicateString(project(db).changes.create(source, str(body, "body"), entryStep));
    });
}

KiottySpecStatus KiottySpec_ChangeRequest_get(KiottySpec *db, const char *crId, KiottySpecChangeRequest **outCr) {
    return guard([&] { *out(outCr) = core::packChangeRequest(project(db).changes.get(str(crId, "crId"))); });
}

KiottySpecStatus KiottySpec_ChangeRequest_setState(KiottySpec *db, KiottySpecActor actor, const char *crId, KiottySpecChangeState state, const char *note) {
    return guard([&] {
        actorOk(actor);
        require(state >= KiottySpecChangeState_Open && state <= KiottySpecChangeState_Rejected, KiottySpecStatus_InvalidArg, "알 수 없는 상태");
        project(db).changes.setState(str(crId, "crId"), state, note != nullptr ? note : "");
    });
}

KiottySpecStatus KiottySpec_ChangeRequest_setEntryStep(KiottySpec *db, KiottySpecActor actor, const char *crId, int32_t entryStep) {
    return guard([&] {
        actorOk(actor);
        project(db).changes.setEntryStep(str(crId, "crId"), entryStep);
    });
}

KiottySpecStatus KiottySpec_ChangeRequest_listBegin(KiottySpec *db, KiottySpecChangeState stateOr0, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), project(db).changes.list(stateOr0)); });
}

KiottySpecStatus KiottySpec_ChangeRequest_openCount(KiottySpec *db, size_t *outCount) {
    return guard([&] { *out(outCount) = project(db).changes.openCount(); });
}

/* ---------------------------------------------------------------- 12. 결정 기록 */

KiottySpecStatus KiottySpec_Decision_get(KiottySpec *db, KiottySpecDecisionKind kind, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = core::duplicateString(project(db).decisions.get(kind)); });
}

KiottySpecStatus KiottySpec_Decision_rewrite(KiottySpec *db, KiottySpecActor actor, KiottySpecDecisionKind kind, const char *markdown) {
    return guard([&] {
        actorOk(actor);
        project(db).decisions.rewrite(kind, str(markdown, "markdown"));
    });
}

KiottySpecStatus KiottySpec_Decision_check(KiottySpec *db, KiottySpecDecisionKind kind, char **outReport) {
    return guard([&] { *out(outReport) = core::duplicateString(project(db).decisions.check(kind)); });
}

KiottySpecStatus KiottySpec_Decision_getAt(KiottySpec *db, KiottySpecDecisionKind kind, const char *cpId, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = core::duplicateString(project(db).decisions.get(kind, cpSeq(db, cpId))); });
}

/* ---------------------------------------------------------------- 13. 결과 */

KiottySpecStatus KiottySpec_Result_record(KiottySpec *db, KiottySpecActor actor, const char *targetId, KiottySpecTestResult result, const char *log) {
    return guard([&] {
        actorOk(actor);
        project(db).results.record(str(targetId, "targetId"), result, log != nullptr ? log : "");
    });
}

KiottySpecStatus KiottySpec_Result_last(KiottySpec *db, const char *targetId, KiottySpecResult **outResult) {
    return guard([&] {
        out(outResult);
        const auto result = project(db).results.last(str(targetId, "targetId"));
        require(result.has_value(), KiottySpecStatus_NotFound, "결과 없음: " + std::string(targetId));
        *outResult = core::packResult(*result);
    });
}

KiottySpecStatus KiottySpec_Result_listBegin(KiottySpec *db, const char *targetId, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), project(db).results.history(str(targetId, "targetId"))); });
}

KiottySpecStatus KiottySpec_Result_failedBegin(KiottySpec *db, KiottySpecItemKind kindOr0, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), project(db).results.failed(kindOr0)); });
}

KiottySpecStatus KiottySpec_Result_summary(KiottySpec *db, KiottySpecItemKind kind, size_t *outTotal, size_t *outPass, size_t *outFail, size_t *outNotRun) {
    return guard([&] {
        const auto summary = project(db).results.summary(kind);
        *out(outTotal) = summary.total;
        *out(outPass) = summary.pass;
        *out(outFail) = summary.fail;
        *out(outNotRun) = summary.notRun;
    });
}

KiottySpecStatus KiottySpec_Result_suspectsBegin(KiottySpec *db, const char *failedTargetId, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), project(db).links.suspects(str(failedTargetId, "failedTargetId"))); });
}

/* ---------------------------------------------------------------- 14. 검사 */

KiottySpecStatus KiottySpec_Check_requirementsWithoutCriteriaBegin(KiottySpec *db, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), project(db).checker.requirementsWithoutCriteria()); });
}

KiottySpecStatus KiottySpec_Check_systemTestOneToOne(KiottySpec *db, KiottySpecCursor **outMissing, KiottySpecCursor **outDuplicated) {
    return guard([&] {
        auto result = project(db).checker.oneToOne(KiottySpecItemKind_SystemTest);
        cursorOf(out(outMissing), std::move(result.missing));
        cursorOf(out(outDuplicated), std::move(result.duplicated));
    });
}

KiottySpecStatus KiottySpec_Check_e2eTestOneToOne(KiottySpec *db, KiottySpecCursor **outMissing, KiottySpecCursor **outDuplicated) {
    return guard([&] {
        auto result = project(db).checker.oneToOne(KiottySpecItemKind_E2eTest);
        cursorOf(out(outMissing), std::move(result.missing));
        cursorOf(out(outDuplicated), std::move(result.duplicated));
    });
}

KiottySpecStatus KiottySpec_Check_unlinkedBegin(KiottySpec *db, KiottySpecItemKind kind, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), project(db).checker.unlinked(kind)); });
}

KiottySpecStatus KiottySpec_Check_orphanTestsBegin(KiottySpec *db, KiottySpecCursor **outCursor) {
    return guard([&] { cursorOf(out(outCursor), project(db).checker.orphanTests()); });
}

KiottySpecStatus KiottySpec_Check_all(KiottySpec *db, char **outReport) {
    return guard([&] { *out(outReport) = core::duplicateString(project(db).checker.all()); });
}

/* ---------------------------------------------------------------- 15. 맥락 */

KiottySpecStatus KiottySpec_Context_forStep(KiottySpec *db, int32_t step, size_t maxBytes, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = core::duplicateString(project(db).context.forStep(step, maxBytes)); });
}

KiottySpecStatus KiottySpec_Context_forItem(KiottySpec *db, const char *itemId, int32_t depth, size_t maxBytes, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = core::duplicateString(project(db).context.forItem(str(itemId, "itemId"), depth, maxBytes)); });
}

KiottySpecStatus KiottySpec_Context_forMilestone(KiottySpec *db, const char *milestoneId, size_t maxBytes, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = core::duplicateString(project(db).context.forMilestone(str(milestoneId, "milestoneId"), maxBytes)); });
}

KiottySpecStatus KiottySpec_Context_forChangeRequest(KiottySpec *db, const char *crId, size_t maxBytes, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = core::duplicateString(project(db).context.forChangeRequest(str(crId, "crId"), maxBytes)); });
}

KiottySpecStatus KiottySpec_Context_rules(KiottySpec *db, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = core::duplicateString(project(db).context.rules()); });
}

KiottySpecStatus KiottySpec_Context_pending(KiottySpec *db, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = core::duplicateString(project(db).context.pending()); });
}

KiottySpecStatus KiottySpec_Context_whereAmI(KiottySpec *db, size_t maxBytes, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = core::duplicateString(project(db).journey.whereAmI(maxBytes)); });
}

KiottySpecStatus KiottySpec_Context_status(KiottySpec *db, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = core::duplicateString(project(db).context.status()); });
}

/* ---------------------------------------------------------------- 16. 내보내기·가져오기 */

KiottySpecStatus KiottySpec_Export_markdown(KiottySpec *db, KiottySpecItemKind kind, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = core::duplicateString(project(db).exchange.exportMarkdown(kind)); });
}

KiottySpecStatus KiottySpec_Export_traceReport(KiottySpec *db, char **outMarkdown) {
    return guard([&] { *out(outMarkdown) = core::duplicateString(project(db).exchange.traceReport()); });
}

KiottySpecStatus KiottySpec_Export_jsonl(KiottySpec *db, const char *dir) {
    return guard([&] {
        auto &p = project(db);
        p.exchange.exportJsonl(dir != nullptr && *dir != '\0' ? core::pathFromUtf8(dir) : p.jsonlDir());
    });
}

KiottySpecStatus KiottySpec_Import_jsonl(KiottySpec *db, const char *dir, bool merge, char **outConflictReport) {
    KiottySpecStatus conflictStatus = KiottySpecStatus_Ok;
    const KiottySpecStatus status = guard([&] {
        auto &p = project(db);
        const auto report = p.exchange.importJsonl(dir != nullptr && *dir != '\0' ? core::pathFromUtf8(dir) : p.jsonlDir(), merge);
        std::string text = std::to_string(report.rows) + " rows\n";
        for (const auto &conflict : report.conflicts) text += "conflict " + conflict + "\n";
        if (outConflictReport != nullptr) *outConflictReport = core::duplicateString(text);
        if (!report.conflicts.empty()) conflictStatus = KiottySpecStatus_Conflict;
    });
    return status != KiottySpecStatus_Ok ? status : conflictStatus;
}

KiottySpecStatus KiottySpec_Import_markdown(KiottySpec *db, KiottySpecActor actor, KiottySpecItemKind kind, const char *markdown, KiottySpecCursor **outCreatedIds) {
    return guard([&] {
        actorOk(actor);
        auto ids = project(db).exchange.importMarkdown(kind, str(markdown, "markdown"));
        if (outCreatedIds != nullptr) cursorOf(outCreatedIds, std::move(ids));
    });
}

KiottySpecStatus KiottySpec_Query_readonly(KiottySpec *db, const char *sql, char **outJson) {
    return guard([&] { *out(outJson) = core::duplicateString(project(db).exchange.queryReadonly(str(sql, "sql"))); });
}

KiottySpecStatus KiottySpec_Schema_version(KiottySpec *db, int32_t *outVersion) {
    return guard([&] { *out(outVersion) = kiotty::base::schema::version(project(db).database()); });
}

KiottySpecStatus KiottySpec_Schema_migrate(KiottySpec *db) {
    return guard([&] { kiotty::base::schema::ensure(project(db).database()); });
}

} // extern "C"
