#include "commands.h"

#include "output.h"

namespace kiotty::cli {

namespace {

template <class Record>
json takeRecord(Record *record) {
    json value = toJson(*record);
    KiottySpec_free(record);
    return value;
}

KiottySpecItemKind kindArg(const std::string &name) { return static_cast<KiottySpecItemKind>(enumValue("ItemKind", name)); }

KiottySpecItemCategory categoryOption(const Arguments &args) {
    const auto category = args.option("category");
    return category ? static_cast<KiottySpecItemCategory>(enumValue("ItemCategory", *category)) : KIOTTY_SPEC_ITEM_CATEGORY_NONE;
}

std::string cpArg(const std::string &text) { return text; }

int stepArg(const std::string &text) { return toInt(text, "step"); }

void printCursor(KiottySpecStatus status, KiottySpecCursor *&cursor, CursorKind kind) {
    check(status);
    printTsv(drain(cursor, kind));
}

void printOwnedText(KiottySpecStatus status, char *&text) {
    check(status);
    printText(take(text));
}

void printVerdict(KiottySpecStatus status, bool &ok, char *&report) {
    check(status);
    printTsv({{"ok", ok}, {"report", take(report)}});
}

std::vector<Command> build() {
    std::vector<Command> list;
    const auto add = [&](std::string group, std::string verb, std::string usage, std::string summary,
                         std::function<void(Session &)> run, bool needsDb = true) {
        list.push_back({std::move(group), std::move(verb), std::move(usage), std::move(summary), needsDb, std::move(run)});
    };

    add("where", "", "[--max-bytes N]", "지금 어느 체크포인트·단계인지, 지나온 단계, 다음 할 일 (오랜만에 돌아왔을 때 먼저)",
        [](Session &s) {
            char *md = nullptr;
            printOwnedText(KiottySpec_Context_whereAmI(s.db, static_cast<size_t>(s.args.intOption("max-bytes", 0)), &md), md);
        });
    add("status", "", "", "항목 수, 마일스톤 진행률, 테스트 집계",
        [](Session &s) { char *md = nullptr; printOwnedText(KiottySpec_Context_status(s.db, &md), md); });

    add("init", "", "[--name 이름] [--hooks]", ".kiotty/spec.db 생성, .gitignore 갱신, --hooks 면 git 훅 설치",
        [](Session &s) {
            const auto name = s.args.option("name").value_or("");
            check(KiottySpec_init(s.db, name.c_str(), s.args.flag("hooks")));
            printTsv({{"root", s.root}, {"initialized", true}});
        });
    add("policy", "set", "KEY VALUE", "정책 설정 (decision_max_line, decision_max_concerns)",
        [](Session &s) {
            check(KiottySpec_setPolicy(s.db, s.args.at(0, "KEY").c_str(), s.args.at(1, "VALUE").c_str()));
            printTsv({{"ok", true}});
        });
    add("policy", "get", "KEY", "정책 조회",
        [](Session &s) { char *v = nullptr; check(KiottySpec_getPolicy(s.db, s.args.at(0, "KEY").c_str(), &v)); printTsv({{"value", take(v)}}); });

    add("checkpoint", "create", "--step N [--note 요약]", "라운드를 마치고 지금까지의 쓰기를 새 체크포인트로 묶음",
        [](Session &s) {
            char *id = nullptr;
            const auto note = s.args.option("note").value_or("");
            check(KiottySpec_Checkpoint_create(s.db, s.actor, stepArg(s.args.required("step")), note.c_str(), &id));
            printTsv({{"checkpoint", take(id)}});
        });
    add("checkpoint", "confirm", "CP --actor user", "체크포인트 확정 (사용자만, 단계별 확정 조건 검사)",
        [](Session &s) {
            const auto cp = s.args.at(0, "CP");
            check(KiottySpec_Checkpoint_confirm(s.db, s.actor, cp.c_str()));
            printTsv({{"confirmed", cp}});
        });
    add("checkpoint", "get", "CP", "체크포인트 하나",
        [](Session &s) { KiottySpecCheckpoint *c = nullptr; check(KiottySpec_Checkpoint_get(s.db, s.args.at(0, "CP").c_str(), &c)); printTsv(takeRecord(c)); });
    add("checkpoint", "current", "", "가장 최근 체크포인트",
        [](Session &s) { KiottySpecCheckpoint *c = nullptr; check(KiottySpec_Checkpoint_current(s.db, &c)); printTsv(takeRecord(c)); });
    add("checkpoint", "position", "", "현재 위치와 다음 할 일 (TSV)",
        [](Session &s) { KiottySpecPosition *p = nullptr; check(KiottySpec_Checkpoint_position(s.db, &p)); printTsv(takeRecord(p)); });
    add("checkpoint", "journey", "", "지금까지 밟아온 단계 구간 (TSV)",
        [](Session &s) { KiottySpecCursor *c = nullptr; printCursor(KiottySpec_Checkpoint_journeyBegin(s.db, &c), c, CursorKind::StepRun); });
    add("checkpoint", "last-confirmed", "[--step N]", "마지막 확정 체크포인트",
        [](Session &s) { KiottySpecCheckpoint *c = nullptr; check(KiottySpec_Checkpoint_lastConfirmed(s.db, s.args.intOption("step", 0), &c)); printTsv(takeRecord(c)); });
    add("checkpoint", "list", "[--step N]", "체크포인트 목록",
        [](Session &s) { KiottySpecCursor *c = nullptr; printCursor(KiottySpec_Checkpoint_listBegin(s.db, s.args.intOption("step", 0), &c), c, CursorKind::Checkpoint); });
    add("checkpoint", "changed", "FROM [TO]", "두 체크포인트 사이에 바뀐 항목 (TO 생략 시 현재까지)",
        [](Session &s) {
            KiottySpecCursor *c = nullptr;
            const auto from = s.args.at(0, "FROM");
            const auto to = s.args.optionalAt(1);
            printCursor(KiottySpec_Checkpoint_changedItemsBegin(s.db, from.c_str(), to ? to->c_str() : nullptr, &c), c, CursorKind::Item);
        });
    add("checkpoint", "diff", "A B", "두 체크포인트 차이 (Markdown)",
        [](Session &s) { char *md = nullptr; printOwnedText(KiottySpec_Checkpoint_diff(s.db, s.args.at(0, "A").c_str(), s.args.at(1, "B").c_str(), &md), md); });
    add("checkpoint", "confirm-check", "STEP", "이 단계를 지금 확정할 수 있는지",
        [](Session &s) { bool ok = false; char *r = nullptr; printVerdict(KiottySpec_Checkpoint_confirmCheck(s.db, stepArg(s.args.at(0, "STEP")), &ok, &r), ok, r); });
    add("checkpoint", "release-check", "", "지금 RELEASE 할 수 있는지",
        [](Session &s) { bool ok = false; char *r = nullptr; printVerdict(KiottySpec_Checkpoint_releaseCheck(s.db, &ok, &r), ok, r); });
    add("checkpoint", "release", "[--note 요약] --actor user", "RELEASE 체크포인트 생성·확정 (사용자만)",
        [](Session &s) {
            char *id = nullptr;
            const auto note = s.args.option("note").value_or("");
            check(KiottySpec_Checkpoint_release(s.db, s.actor, note.c_str(), &id));
            printTsv({{"release", take(id)}});
        });
    add("checkpoint", "prune", "CP --actor user", "CP 이전 기록을 모두 지움 (Compaction, 사용자만)",
        [](Session &s) {
            size_t removed = 0;
            check(KiottySpec_Checkpoint_prune(s.db, s.actor, cpArg(s.args.at(0, "CP")).c_str(), &removed));
            printTsv({{"removedRows", removed}});
        });

    add("item", "create", "KIND [--category C] [--parent ID] --title T [--body 본문|--body-file F|--body -]",
        "항목 생성. KIND: requirement rule interface design unit_test system_test e2e_test milestone",
        [](Session &s) {
            char *id = nullptr;
            const auto parent = s.args.option("parent");
            const auto title = s.args.required("title");
            const auto body = s.args.text("body").value_or("");
            check(KiottySpec_Item_create(s.db, s.actor, kindArg(s.args.at(0, "KIND")), categoryOption(s.args),
                                         parent ? parent->c_str() : nullptr, title.c_str(), body.c_str(), &id));
            printTsv({{"id", take(id)}});
        });
    add("item", "get", "ID [--at CP]", "항목 하나 (현재 또는 CP 시점)",
        [](Session &s) {
            KiottySpecItem *item = nullptr;
            const auto id = s.args.at(0, "ID");
            if (const auto at = s.args.option("at")) check(KiottySpec_Item_getAt(s.db, id.c_str(), at->c_str(), &item));
            else check(KiottySpec_Item_get(s.db, id.c_str(), &item));
            printTsv(takeRecord(item));
        });
    add("item", "update", "ID [--title T] [--body 본문|--body-file F]", "제목·본문 수정",
        [](Session &s) {
            const auto title = s.args.option("title");
            const auto body = s.args.text("body");
            check(KiottySpec_Item_update(s.db, s.actor, s.args.at(0, "ID").c_str(), title ? title->c_str() : nullptr,
                                         body ? body->c_str() : nullptr));
            printTsv({{"updated", s.args.at(0, "ID")}});
        });
    add("item", "set-parent", "ID [--parent P] [--seq N]", "상위 항목·순서 변경 (--parent 생략 시 최상위)",
        [](Session &s) {
            const auto parent = s.args.option("parent");
            check(KiottySpec_Item_setParent(s.db, s.actor, s.args.at(0, "ID").c_str(), parent ? parent->c_str() : nullptr,
                                            s.args.intOption("seq", 0)));
            printTsv({{"updated", s.args.at(0, "ID")}});
        });
    add("item", "remove", "ID [--reason 이유]", "현재 체크포인트에서 항목과 그 링크를 제거",
        [](Session &s) {
            const auto reason = s.args.option("reason").value_or("");
            check(KiottySpec_Item_remove(s.db, s.actor, s.args.at(0, "ID").c_str(), reason.c_str()));
            printTsv({{"removed", s.args.at(0, "ID")}});
        });
    add("item", "list", "[--kind K] [--category C] [--parent P] [--text T] [--at CP]", "필터로 항목 열거",
        [](Session &s) {
            KiottySpecItemFilter filter{};
            const auto kind = s.args.option("kind");
            const auto parent = s.args.option("parent");
            const auto text = s.args.option("text");
            const auto at = s.args.option("at");
            if (kind) filter.kind = kindArg(*kind);
            filter.category = categoryOption(s.args);
            filter.parentId = parent ? parent->c_str() : nullptr;
            filter.text = text ? text->c_str() : nullptr;
            filter.atCheckpoint = at ? at->c_str() : nullptr;
            KiottySpecCursor *c = nullptr;
            printCursor(KiottySpec_Item_listBegin(s.db, &filter, &c), c, CursorKind::Item);
        });
    add("item", "children", "ID [--recursive]", "하위 항목",
        [](Session &s) { KiottySpecCursor *c = nullptr; printCursor(KiottySpec_Item_childrenBegin(s.db, s.args.at(0, "ID").c_str(), s.args.flag("recursive"), &c), c, CursorKind::Item); });
    add("item", "search", "QUERY [--limit N]", "제목·본문 검색",
        [](Session &s) {
            KiottySpecCursor *c = nullptr;
            printCursor(KiottySpec_Item_searchBegin(s.db, s.args.at(0, "QUERY").c_str(), static_cast<size_t>(s.args.intOption("limit", 50)), &c), c, CursorKind::Item);
        });
    add("item", "template", "KIND [--category C]", "종류별 본문 틀",
        [](Session &s) { char *md = nullptr; printOwnedText(KiottySpec_Item_template(kindArg(s.args.at(0, "KIND")), categoryOption(s.args), &md), md); },
        false);

    add("milestone", "set-state", "MS ITEM not_implemented|needs_change|implemented", "포함 항목의 구현 상태 기록",
        [](Session &s) {
            const auto state = static_cast<KiottySpecImplementationState>(enumValue("ImplState", s.args.at(2, "STATE")));
            check(KiottySpec_Milestone_setEntryState(s.db, s.actor, s.args.at(0, "MS").c_str(), s.args.at(1, "ITEM").c_str(), state));
            printTsv({{"ok", true}});
        });
    add("milestone", "entries", "MS", "포함 항목과 계산된 구현 상태",
        [](Session &s) { KiottySpecCursor *c = nullptr; printCursor(KiottySpec_Milestone_entriesBegin(s.db, s.args.at(0, "MS").c_str(), &c), c, CursorKind::MilestoneEntry); });
    add("milestone", "uncovered", "", "어떤 마일스톤에도 없는 설계 항목·유닛 테스트",
        [](Session &s) { KiottySpecCursor *c = nullptr; printCursor(KiottySpec_Milestone_uncoveredBegin(s.db, &c), c, CursorKind::Item); });
    add("milestone", "next", "", "다음에 구현할 마일스톤",
        [](Session &s) { char *id = nullptr; check(KiottySpec_Milestone_next(s.db, &id)); printTsv({{"milestone", take(id)}}); });
    add("milestone", "progress", "", "구현 진행률",
        [](Session &s) {
            size_t total = 0, done = 0, change = 0;
            check(KiottySpec_Milestone_progress(s.db, &total, &done, &change));
            printTsv({{"total", total}, {"implemented", done}, {"needsChange", change}});
        });

    add("file", "register", "ROLE PATH", "파일 등록 (ROLE: gui_draft c_header source)",
        [](Session &s) {
            char *id = nullptr;
            const auto role = static_cast<KiottySpecFileRole>(enumValue("FileRole", s.args.at(0, "ROLE")));
            check(KiottySpec_File_register(s.db, s.actor, role, s.args.at(1, "PATH").c_str(), &id));
            printTsv({{"id", take(id)}});
        });
    add("file", "get", "ID", "파일 하나",
        [](Session &s) { KiottySpecFile *f = nullptr; check(KiottySpec_File_get(s.db, s.args.at(0, "ID").c_str(), &f)); printTsv(takeRecord(f)); });
    add("file", "by-path", "PATH", "경로로 파일 조회",
        [](Session &s) { KiottySpecFile *f = nullptr; check(KiottySpec_File_byPath(s.db, s.args.at(0, "PATH").c_str(), &f)); printTsv(takeRecord(f)); });
    add("file", "rescan", "ID", "해시 재계산",
        [](Session &s) { bool changed = false; check(KiottySpec_File_rescan(s.db, s.actor, s.args.at(0, "ID").c_str(), &changed)); printTsv({{"changed", changed}}); });
    add("file", "list", "[--role R]", "파일 목록",
        [](Session &s) {
            const auto role = s.args.option("role");
            KiottySpecCursor *c = nullptr;
            printCursor(KiottySpec_File_listBegin(s.db, role ? static_cast<KiottySpecFileRole>(enumValue("FileRole", *role)) : static_cast<KiottySpecFileRole>(0), &c), c, CursorKind::File);
        });
    add("file", "changed-since", "CP", "CP 이후 해시가 바뀐 파일",
        [](Session &s) { KiottySpecCursor *c = nullptr; printCursor(KiottySpec_File_changedSinceBegin(s.db, s.args.at(0, "CP").c_str(), &c), c, CursorKind::File); });
    add("file", "scan-gui", "ID", "GUI 초안 html 의 data-ks-id 와 DB 불일치",
        [](Session &s) {
            KiottySpecCursor *unknown = nullptr;
            KiottySpecCursor *missing = nullptr;
            check(KiottySpec_File_scanGuiDraft(s.db, s.args.at(0, "ID").c_str(), &unknown, &missing));
            json result{{"unknownIds", drain(unknown, CursorKind::Id)}};
            result["missingViews"] = drain(missing, CursorKind::Item);
            printTsv(result);
        });

    add("link", "add", "FROM TYPE TO", "링크 \"FROM 은 TO 를 ~한다\" (TYPE: satisfies verifies implements covers depends_on)",
        [](Session &s) {
            const auto type = static_cast<KiottySpecLinkType>(enumValue("LinkType", s.args.at(1, "TYPE")));
            check(KiottySpec_Link_add(s.db, s.actor, s.args.at(0, "FROM").c_str(), s.args.at(2, "TO").c_str(), type));
            printTsv({{"from", s.args.at(0, "FROM")}, {"type", s.args.at(1, "TYPE")}, {"to", s.args.at(2, "TO")}});
        });
    add("link", "remove", "FROM TYPE TO", "링크 제거",
        [](Session &s) {
            const auto type = static_cast<KiottySpecLinkType>(enumValue("LinkType", s.args.at(1, "TYPE")));
            check(KiottySpec_Link_remove(s.db, s.actor, s.args.at(0, "FROM").c_str(), s.args.at(2, "TO").c_str(), type));
            printTsv({{"removed", true}});
        });
    add("link", "list", "ID [--dir up|down|both] [--type T]", "항목·파일에 걸린 링크",
        [](Session &s) {
            const auto dir = static_cast<KiottySpecDirection>(enumValue("Direction", s.args.option("dir").value_or("both")));
            const auto type = s.args.option("type");
            KiottySpecCursor *c = nullptr;
            printCursor(KiottySpec_Link_listBegin(s.db, s.args.at(0, "ID").c_str(), dir,
                                                  type ? static_cast<KiottySpecLinkType>(enumValue("LinkType", *type)) : static_cast<KiottySpecLinkType>(0), &c),
                        c, CursorKind::Link);
        });
    add("link", "up", "ID [--depth N]", "요구사항 방향 추적 (왜 있는가)",
        [](Session &s) { KiottySpecCursor *c = nullptr; printCursor(KiottySpec_Link_upstreamBegin(s.db, s.args.at(0, "ID").c_str(), s.args.intOption("depth", 0), &c), c, CursorKind::Id); });
    add("link", "down", "ID [--depth N]", "코드 방향 추적 (어디에 구현·검증되는가)",
        [](Session &s) { KiottySpecCursor *c = nullptr; printCursor(KiottySpec_Link_downstreamBegin(s.db, s.args.at(0, "ID").c_str(), s.args.intOption("depth", 0), &c), c, CursorKind::Id); });
    add("link", "impact", "CP", "CP 이후 바뀐 항목과 링크된 영향 항목",
        [](Session &s) { KiottySpecCursor *c = nullptr; printCursor(KiottySpec_Link_impactBegin(s.db, s.args.at(0, "CP").c_str(), &c), c, CursorKind::Item); });
    add("link", "scope", "ID", "수정이 모듈 내인지 모듈 간인지",
        [](Session &s) {
            KiottySpecScope scope{};
            KiottySpecCursor *c = nullptr;
            check(KiottySpec_Link_scopeOf(s.db, s.args.at(0, "ID").c_str(), &scope, &c));
            json result{{"scope", enumName("Scope", scope)}};
            result["affectedModules"] = drain(c, CursorKind::Id);
            printTsv(result);
        });
    add("link", "matrix", "ROW_KIND COL_KIND", "추적 매트릭스 CSV",
        [](Session &s) { char *csv = nullptr; printOwnedText(KiottySpec_Link_matrix(s.db, kindArg(s.args.at(0, "ROW_KIND")), kindArg(s.args.at(1, "COL_KIND")), &csv), csv); });

    add("cr", "create", "--source feedback|issue --entry-step N --body 내용", "변경 요청 등록",
        [](Session &s) {
            char *id = nullptr;
            const auto source = static_cast<KiottySpecChangeSource>(enumValue("ChangeSource", s.args.option("source").value_or("feedback")));
            const auto body = s.args.text("body");
            if (!body) throw UsageError("--body 가 필요함");
            check(KiottySpec_ChangeRequest_create(s.db, s.actor, source, body->c_str(), stepArg(s.args.required("entry-step")), &id));
            printTsv({{"id", take(id)}});
        });
    add("cr", "get", "CR", "변경 요청 하나",
        [](Session &s) { KiottySpecChangeRequest *c = nullptr; check(KiottySpec_ChangeRequest_get(s.db, s.args.at(0, "CR").c_str(), &c)); printTsv(takeRecord(c)); });
    add("cr", "set-state", "CR open|in_progress|done|rejected [--note 메모]", "상태 변경",
        [](Session &s) {
            const auto state = static_cast<KiottySpecChangeState>(enumValue("ChangeState", s.args.at(1, "STATE")));
            const auto note = s.args.option("note").value_or("");
            check(KiottySpec_ChangeRequest_setState(s.db, s.actor, s.args.at(0, "CR").c_str(), state, note.c_str()));
            printTsv({{"ok", true}});
        });
    add("cr", "set-entry-step", "CR N", "진입 단계 정정",
        [](Session &s) { check(KiottySpec_ChangeRequest_setEntryStep(s.db, s.actor, s.args.at(0, "CR").c_str(), stepArg(s.args.at(1, "N")))); printTsv({{"ok", true}}); });
    add("cr", "list", "[--state S]", "변경 요청 목록",
        [](Session &s) {
            const auto state = s.args.option("state");
            KiottySpecCursor *c = nullptr;
            printCursor(KiottySpec_ChangeRequest_listBegin(s.db, state ? static_cast<KiottySpecChangeState>(enumValue("ChangeState", *state)) : static_cast<KiottySpecChangeState>(0), &c), c, CursorKind::ChangeRequest);
        });

    add("decision", "get", "KIND [--at CP]", "결정 기록 (KIND: requirement rule interface design unit_test plan)",
        [](Session &s) {
            char *md = nullptr;
            const auto kind = static_cast<KiottySpecDecisionKind>(enumValue("DecisionKind", s.args.at(0, "KIND")));
            if (const auto at = s.args.option("at")) printOwnedText(KiottySpec_Decision_getAt(s.db, kind, at->c_str(), &md), md);
            else printOwnedText(KiottySpec_Decision_get(s.db, kind, &md), md);
        });
    add("decision", "rewrite", "KIND --body 문서|--body-file F", "결정 기록을 통째로 다시 씀 (줄: \"- ID · 관심사 : 결정 (이유) [기각: …]\")",
        [](Session &s) {
            const auto kind = static_cast<KiottySpecDecisionKind>(enumValue("DecisionKind", s.args.at(0, "KIND")));
            const auto body = s.args.text("body");
            if (!body) throw UsageError("--body 가 필요함");
            check(KiottySpec_Decision_rewrite(s.db, s.actor, kind, body->c_str()));
            printTsv({{"ok", true}});
        });
    add("decision", "check", "KIND", "사라진 항목을 가리키는 줄, 길이 초과",
        [](Session &s) {
            char *r = nullptr;
            check(KiottySpec_Decision_check(s.db, static_cast<KiottySpecDecisionKind>(enumValue("DecisionKind", s.args.at(0, "KIND"))), &r));
            const auto report = take(r);
            printTsv({{"ok", report.empty()}, {"report", report}});
        });

    add("result", "record", "TARGET pass|fail [--log 출력|--log-file F]", "테스트 또는 (대상이 규칙이면) 코드 검사 결과 기록",
        [](Session &s) {
            const auto result = static_cast<KiottySpecTestResult>(enumValue("TestResult", s.args.at(1, "RESULT")));
            const auto log = s.args.text("log").value_or("");
            check(KiottySpec_Result_record(s.db, s.actor, s.args.at(0, "TARGET").c_str(), result, log.c_str()));
            printTsv({{"ok", true}});
        });
    add("result", "last", "TARGET", "마지막 결과",
        [](Session &s) { KiottySpecResult *r = nullptr; check(KiottySpec_Result_last(s.db, s.args.at(0, "TARGET").c_str(), &r)); printTsv(takeRecord(r)); });
    add("result", "list", "TARGET", "결과 이력",
        [](Session &s) { KiottySpecCursor *c = nullptr; printCursor(KiottySpec_Result_listBegin(s.db, s.args.at(0, "TARGET").c_str(), &c), c, CursorKind::Result); });
    add("result", "failed", "[--kind K]", "마지막 결과가 FAIL 인 테스트·규칙",
        [](Session &s) {
            const auto kind = s.args.option("kind");
            KiottySpecCursor *c = nullptr;
            printCursor(KiottySpec_Result_failedBegin(s.db, kind ? kindArg(*kind) : static_cast<KiottySpecItemKind>(0), &c), c, CursorKind::Item);
        });
    add("result", "summary", "KIND", "통과·실패·미실행 집계",
        [](Session &s) {
            size_t total = 0, pass = 0, fail = 0, notRun = 0;
            check(KiottySpec_Result_summary(s.db, kindArg(s.args.at(0, "KIND")), &total, &pass, &fail, &notRun));
            printTsv({{"total", total}, {"pass", pass}, {"fail", fail}, {"notRun", notRun}});
        });
    add("result", "suspects", "TARGET", "실패한 테스트의 원인 후보 설계 항목·소스 파일",
        [](Session &s) { KiottySpecCursor *c = nullptr; printCursor(KiottySpec_Result_suspectsBegin(s.db, s.args.at(0, "TARGET").c_str(), &c), c, CursorKind::Id); });

    add("check", "criteria", "", "인수 조건이 없는 기능 요구사항",
        [](Session &s) { KiottySpecCursor *c = nullptr; printCursor(KiottySpec_Check_requirementsWithoutCriteriaBegin(s.db, &c), c, CursorKind::Item); });
    add("check", "st", "", "시스템 테스트 1:1 검사",
        [](Session &s) {
            KiottySpecCursor *missing = nullptr, *dup = nullptr;
            check(KiottySpec_Check_systemTestOneToOne(s.db, &missing, &dup));
            json result{{"missing", drain(missing, CursorKind::Id)}};
            result["duplicated"] = drain(dup, CursorKind::Id);
            printTsv(result);
        });
    add("check", "e2e", "", "E2E 테스트 1:1 검사",
        [](Session &s) {
            KiottySpecCursor *missing = nullptr, *dup = nullptr;
            check(KiottySpec_Check_e2eTestOneToOne(s.db, &missing, &dup));
            json result{{"missing", drain(missing, CursorKind::Id)}};
            result["duplicated"] = drain(dup, CursorKind::Id);
            printTsv(result);
        });
    add("check", "unlinked", "KIND", "링크가 없는 항목",
        [](Session &s) { KiottySpecCursor *c = nullptr; printCursor(KiottySpec_Check_unlinkedBegin(s.db, kindArg(s.args.at(0, "KIND")), &c), c, CursorKind::Id); });
    add("check", "orphan-tests", "", "검증 대상이 없는 테스트",
        [](Session &s) { KiottySpecCursor *c = nullptr; printCursor(KiottySpec_Check_orphanTestsBegin(s.db, &c), c, CursorKind::Id); });
    add("check", "all", "", "전체 검사 보고서 (Markdown)",
        [](Session &s) { char *md = nullptr; printOwnedText(KiottySpec_Check_all(s.db, &md), md); });

    add("context", "step", "N [--max-bytes N]", "그 단계의 입력을 한 번에 (definition.md 4절)",
        [](Session &s) {
            char *md = nullptr;
            printOwnedText(KiottySpec_Context_forStep(s.db, stepArg(s.args.at(0, "N")), static_cast<size_t>(s.args.intOption("max-bytes", 0)), &md), md);
        });
    add("context", "item", "ID [--depth N] [--max-bytes N]", "항목과 상·하류, 관련 결정 줄",
        [](Session &s) {
            char *md = nullptr;
            printOwnedText(KiottySpec_Context_forItem(s.db, s.args.at(0, "ID").c_str(), s.args.intOption("depth", 2),
                                                      static_cast<size_t>(s.args.intOption("max-bytes", 0)), &md), md);
        });
    add("context", "milestone", "MS [--max-bytes N]", "마일스톤 구현에 필요한 것",
        [](Session &s) { char *md = nullptr; printOwnedText(KiottySpec_Context_forMilestone(s.db, s.args.at(0, "MS").c_str(), static_cast<size_t>(s.args.intOption("max-bytes", 0)), &md), md); });
    add("context", "cr", "CR [--max-bytes N]", "변경 요청의 영향 항목",
        [](Session &s) { char *md = nullptr; printOwnedText(KiottySpec_Context_forChangeRequest(s.db, s.args.at(0, "CR").c_str(), static_cast<size_t>(s.args.intOption("max-bytes", 0)), &md), md); });
    add("context", "rules", "", "코드 작성 전 읽을 개발 규칙",
        [](Session &s) { char *md = nullptr; printOwnedText(KiottySpec_Context_rules(s.db, &md), md); });
    add("context", "pending", "", "지금 할 일",
        [](Session &s) { char *md = nullptr; printOwnedText(KiottySpec_Context_pending(s.db, &md), md); });

    add("export", "markdown", "KIND", "종류별 문서",
        [](Session &s) { char *md = nullptr; printOwnedText(KiottySpec_Export_markdown(s.db, kindArg(s.args.at(0, "KIND")), &md), md); });
    add("export", "trace", "", "추적 매트릭스 + 검사 보고서",
        [](Session &s) { char *md = nullptr; printOwnedText(KiottySpec_Export_traceReport(s.db, &md), md); });
    add("export", "jsonl", "[DIR]", "DB 를 jsonl 로 (기본 .kiotty/spec, pre-commit 훅)",
        [](Session &s) {
            const auto dir = s.args.optionalAt(0).value_or("");
            check(KiottySpec_Export_jsonl(s.db, dir.c_str()));
            printTsv({{"ok", true}});
        });
    add("import", "jsonl", "[DIR] [--merge]", "jsonl 로 DB 재생성·병합 (post-merge 훅)",
        [](Session &s) {
            const auto dir = s.args.optionalAt(0).value_or("");
            char *report = nullptr;
            const KiottySpecStatus status = KiottySpec_Import_jsonl(s.db, dir.c_str(), s.args.flag("merge"), &report);
            if (status != KIOTTY_SPEC_STATUS_SUCCESS && status != KIOTTY_SPEC_STATUS_CONFLICT) check(status);
            printTsv({{"conflict", status == KIOTTY_SPEC_STATUS_CONFLICT}, {"report", take(report)}});
        });
    add("import", "markdown", "KIND --body 문서|--body-file F", "\"## [category] 제목\" 절마다 항목 하나로 가져옴",
        [](Session &s) {
            const auto body = s.args.text("body");
            if (!body) throw UsageError("--body 가 필요함");
            KiottySpecCursor *c = nullptr;
            printCursor(KiottySpec_Import_markdown(s.db, s.actor, kindArg(s.args.at(0, "KIND")), body->c_str(), &c), c, CursorKind::Id);
        });

    add("query", "", "SQL", "읽기 전용 SQL (TSV)",
        [](Session &s) {
            char *out = nullptr;
            check(KiottySpec_Query_readonly(s.db, s.args.at(0, "SQL").c_str(), &out));
            printOrderedTsv(nlohmann::ordered_json::parse(take(out)));
        });
    add("schema", "version", "", "스키마 버전",
        [](Session &s) { int32_t v = 0; check(KiottySpec_Schema_version(s.db, &v)); printTsv({{"version", v}}); });
    add("schema", "migrate", "", "스키마 올리기",
        [](Session &s) { check(KiottySpec_Schema_migrate(s.db)); printTsv({{"ok", true}}); });
    return list;
}

}

const std::vector<Command> &commands() {
    static const std::vector<Command> list = build();
    return list;
}

std::string helpText() {
    std::string text =
        "ks — Kiotty-Spec CLI\n\n"
        "사용법: ks <그룹> [동사] [인자...] [--root DIR] [--actor ai|user]\n"
        "  --root  프로젝트 루트 (기본: 현재 위치에서 위로 .kiotty 를 찾음, 환경변수 KIOTTY_SPEC_ROOT)\n"
        "  --actor 기본 ai. 확정·RELEASE·prune 은 --actor user 필요 (환경변수 KIOTTY_SPEC_ACTOR)\n"
        "  본문은 --body 텍스트, --body-file 경로, --body - (표준 입력) 중 하나\n"
        "  출력: 레코드·목록은 TSV (첫 줄이 열 이름, 탭으로 구분, 값의 \\t \\n \\r \\\\ 는 이스케이프), 문서는 Markdown. 실패 시 stderr 에 'error: <상태>: <메시지>', 종료 코드 1\n\n"
        "처음이거나 오랜만이면: ks where\n\n";
    std::string group;
    for (const auto &command : commands()) {
        if (command.group != group) {
            group = command.group;
            text += "\n";
        }
        std::string line = "  ks " + command.group + (command.verb.empty() ? "" : " " + command.verb);
        if (!command.usage.empty()) line += " " + command.usage;
        text += line + "\n      " + command.summary + "\n";
    }
    return text;
}

}
