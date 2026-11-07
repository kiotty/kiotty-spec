#include "implementation/context/project.h"

#include "implementation/data/datasource/schema.h"
#include "implementation/core/error.h"
#include "implementation/core/text.h"

namespace kiotty::spec::implementation {

namespace {

constexpr std::string_view kPreCommit =
    "#!/bin/sh\n"
    "# kiotty-spec: 커밋 전에 DB 를 jsonl 로 내보내고 스테이징한다.\n"
    "ks export jsonl || exit 1\n"
    "git add .kiotty/spec\n";

constexpr std::string_view kPostSync =
    "#!/bin/sh\n"
    "# kiotty-spec: 병합·체크아웃 뒤 jsonl 로 DB 를 다시 만든다.\n"
    "[ -d .kiotty/spec ] && ks import jsonl\n"
    "exit 0\n";

void requireUser(KiottySpecActor actor, std::string_view what) {
    require(actor == KIOTTY_SPEC_ACTOR_USER, KIOTTY_SPEC_STATUS_PERMISSION,
                  std::string(what) + ": 사용자만 할 수 있음 (R-2)");
}

}

std::filesystem::path Project::prepareRoot(const std::filesystem::path &root) {
    std::error_code ec;
    require(std::filesystem::is_directory(root, ec), KIOTTY_SPEC_STATUS_INPUT_OUTPUT,
                  "프로젝트 루트가 디렉토리가 아님: " + pathToUtf8(root));
    const auto absolute = std::filesystem::absolute(root, ec);
    std::filesystem::create_directories(absolute / ".kiotty", ec);
    require(!ec, KIOTTY_SPEC_STATUS_INPUT_OUTPUT, ".kiotty 디렉토리를 만들 수 없음");
    return absolute;
}

Project::Project(const std::filesystem::path &root)
    : root_(prepareRoot(root)),
      db_(pathToUtf8(root_ / ".kiotty" / "spec.db")),
      meta(db_),
      checkpoints(db_),
      items(db_, meta),
      files(db_, meta, root_),
      links(db_, items, files),
      milestones(db_, meta, items, links),
      changes(db_, meta),
      decisions(db_, meta, items),
      results(db_, items),
      checker(items, links, milestones, results, decisions, changes),
      context(Stores{checkpoints, items, files, links, milestones, changes, decisions, results, checker}),
      exchange(db_, items, links, checker, context),
      journey(Stores{checkpoints, items, files, links, milestones, changes, decisions, results, checker}, meta) {
    ensure(db_);
}

void Project::init(std::string_view projectName, bool installGitHooks) {
    meta.set("project.name", projectName.empty() ? pathToUtf8(root_.filename()) : std::string(projectName));
    meta.set("project.created_at", std::to_string(nowEpochSeconds()));

    const auto gitignore = root_ / ".gitignore";
    std::string content = readFile(gitignore).value_or("");
    const std::string_view entries[] = {".kiotty/spec.db", ".kiotty/spec.db-wal", ".kiotty/spec.db-shm"};
    for (const auto entry : entries) {
        if (content.find(entry) == std::string::npos) {
            if (!content.empty() && content.back() != '\n') content += "\n";
            content += std::string(entry) + "\n";
        }
    }
    writeFile(gitignore, content);

    if (installGitHooks) {
        installHooks();
    }
}

void Project::installHooks() {
    std::error_code ec;
    const auto hooks = root_ / ".git" / "hooks";
    require(std::filesystem::is_directory(root_ / ".git", ec), KIOTTY_SPEC_STATUS_PRECONDITION,
                  "git 저장소가 아님 (.git 없음): " + pathToUtf8(root_));
    std::filesystem::create_directories(hooks, ec);
    writeFile(hooks / "pre-commit", kPreCommit);
    writeFile(hooks / "post-merge", kPostSync);
    writeFile(hooks / "post-checkout", kPostSync);
    for (const char *name : {"pre-commit", "post-merge", "post-checkout"}) {
        std::filesystem::permissions(hooks / name,
                                     std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
                                         std::filesystem::perms::others_exec,
                                     std::filesystem::perm_options::add, ec);
    }
}

int64_t Project::confirm(KiottySpecActor actor, std::string_view cpId) {
    requireUser(actor, "체크포인트 확정");
    const int64_t seq = checkpoints.resolve(cpId);
    require(seq != kPending, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "PENDING 은 확정할 수 없음. 먼저 체크포인트를 찍을 것");
    const auto record = checkpoints.get(seq);
    if (record.confirmed) {
        return seq;
    }
    const auto verdict = checker.confirmCheck(record.step, seq);
    require(verdict.ok, KIOTTY_SPEC_STATUS_PRECONDITION,
                  "Step " + std::to_string(record.step) + " 확정 조건 미충족:\n" + verdict.report);
    checkpoints.markConfirmed(seq);
    return seq;
}

int64_t Project::release(KiottySpecActor actor, std::string_view note) {
    requireUser(actor, "RELEASE");
    const auto verdict = checker.releaseCheck();
    require(verdict.ok, KIOTTY_SPEC_STATUS_PRECONDITION, "RELEASE 조건 미충족 (R-10):\n" + verdict.report);
    LegacySavepoint savepoint(db_);
    const int64_t seq = checkpoints.create(10, note, KIOTTY_SPEC_CHECKPOINT_PURPOSE_RELEASE);
    checkpoints.markConfirmed(seq);
    savepoint.commit();
    return seq;
}

size_t Project::prune(KiottySpecActor actor, std::string_view baseCpId) {
    requireUser(actor, "체크포인트 정리");
    const int64_t base = checkpoints.resolve(baseCpId);
    require(base != kPending, KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, "PENDING 은 기준이 될 수 없음");
    require(checkpoints.get(base).confirmed, KIOTTY_SPEC_STATUS_PRECONDITION, "기준 체크포인트는 확정된 것이어야 함");
    for (const auto &record : checkpoints.list(0)) {
        require(!(record.seq < base && record.purpose == KIOTTY_SPEC_CHECKPOINT_PURPOSE_RELEASE),
                      KIOTTY_SPEC_STATUS_PRECONDITION,
                      "RELEASE 체크포인트 " + checkpointName(record.seq) + " 는 지울 수 없음. 그 이후를 기준으로 할 것");
    }
    return checkpoints.prune(base);
}

void Project::removeItem(std::string_view itemId) {
    LegacySavepoint savepoint(db_);
    items.get(itemId);
    links.removeAllFor(itemId);
    items.remove(itemId);
    savepoint.commit();
}

void Project::beginTransaction() { db_.exec("BEGIN IMMEDIATE"); }
void Project::commitTransaction() { db_.exec("COMMIT"); }
void Project::rollbackTransaction() { db_.exec("ROLLBACK"); }

}
