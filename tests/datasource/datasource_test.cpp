#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/datasource/kiotty_connection.h"
#include "implementation/data/datasource/kiotty_savepoint.h"

#include <cstdio>
#include <utility>

namespace core = kiotty::spec::implementation;

namespace {

using Test = bool (*)();

bool expectTrue(bool condition, const char* label)
{
    if (!condition) { std::printf("  FAIL: %s\n", label); }
    return condition;
}

bool openMemory(core::Connection& connection)
{
    const bool opened = connection.open(core::literalText(":memory:")) == KIOTTY_SPEC_STATUS_SUCCESS;
    const bool created = connection.execute(core::literalText("CREATE TABLE t (a INTEGER, b TEXT);")) == KIOTTY_SPEC_STATUS_SUCCESS;
    return opened && created;
}

int64_t countRows(core::Connection& connection)
{
    auto prepared = connection.prepare(core::literalText("SELECT COUNT(*) FROM t"));
    prepared.value.step();
    return prepared.value.row().integer(0);
}

bool testTextView()
{
    const char text[] = "abcdef";
    bool ok = true;
    ok = expectTrue(core::boundedText(text, 3).length() == 3, "bounded text stops at limit") && ok;
    ok = expectTrue(core::boundedText(text, 100).length() == 6, "bounded text stops at terminator") && ok;
    ok = expectTrue(core::boundedText(nullptr, 5).length() == 0, "bounded text of null is empty") && ok;
    ok = expectTrue(core::literalText("xyz").length() == 3, "literal text drops terminator") && ok;
    return ok;
}

bool testBindAndRead()
{
    core::Connection connection;
    bool ok = expectTrue(openMemory(connection), "open memory database");
    auto insert = connection.prepare(core::literalText("INSERT INTO t VALUES (?1, ?2)"));
    ok = expectTrue(static_cast<bool>(insert), "prepare insert") && ok;
    ok = expectTrue(insert.value.bind(1, static_cast<int64_t>(7)) == KIOTTY_SPEC_STATUS_SUCCESS, "bind integer") && ok;
    ok = expectTrue(insert.value.bind(2, core::literalText("\xED\x95\x9C")) == KIOTTY_SPEC_STATUS_SUCCESS, "bind text") && ok;
    ok = expectTrue(insert.value.run() == KIOTTY_SPEC_STATUS_SUCCESS, "run insert") && ok;
    ok = expectTrue(connection.changedRowCount() == 1 && connection.insertedRowId() == 1, "insert counters") && ok;
    auto select = connection.prepare(core::literalText("SELECT a, b, NULL FROM t"));
    const auto stepped = select.value.step();
    const core::Row row = select.value.row();
    ok = expectTrue(stepped.code == KIOTTY_SPEC_STATUS_SUCCESS && stepped.value, "step yields row") && ok;
    ok = expectTrue(row.integer(0) == 7 && row.text(1).length() == 3, "row values") && ok;
    ok = expectTrue(row.type(0) == core::ColumnType::INTEGER && row.isNull(2), "row types") && ok;
    ok = expectTrue(row.count() == 3 && row.name(1).length() == 1, "row shape") && ok;
    ok = expectTrue(select.value.isReadOnly() && !insert.value.isReadOnly(), "read only detection") && ok;
    return ok;
}

bool testEmptyTextIsNotNull()
{
    core::Connection connection;
    bool ok = expectTrue(openMemory(connection), "open memory database");
    auto query = connection.prepare(core::literalText("SELECT ?1 = '', ?1 IS NULL"));
    query.value.bind(1, core::Span<const char>());
    const auto stepped = query.value.step();
    ok = expectTrue(stepped.value && query.value.row().integer(0) == 1, "empty span binds empty text") && ok;
    ok = expectTrue(query.value.row().integer(1) == 0, "empty span is not null") && ok;
    return ok;
}

bool testBindFailureIsKept()
{
    core::Connection connection;
    bool ok = expectTrue(openMemory(connection), "open memory database");
    auto query = connection.prepare(core::literalText("SELECT ?1"));
    ok = expectTrue(query.value.bind(5, static_cast<int64_t>(1)) == KIOTTY_SPEC_STATUS_DATABASE, "out of range bind fails") && ok;
    query.value.bind(1, static_cast<int64_t>(1));
    ok = expectTrue(query.value.step().code == KIOTTY_SPEC_STATUS_DATABASE, "step reports earlier bind failure") && ok;
    ok = expectTrue(query.value.run() == KIOTTY_SPEC_STATUS_DATABASE, "run reports earlier bind failure") && ok;
    return ok;
}

bool testFailures()
{
    core::Connection connection;
    bool ok = expectTrue(openMemory(connection), "open memory database");
    const auto bad = connection.prepare(core::literalText("SELECT nope FROM nowhere"));
    ok = expectTrue(bad.code == KIOTTY_SPEC_STATUS_DATABASE, "bad sql fails to prepare") && ok;
    ok = expectTrue(connection.message().length() > 0, "failure leaves message") && ok;
    const KiottySpecStatus status = connection.execute(core::literalText("SELECT 1; BROKEN;"));
    ok = expectTrue(status == KIOTTY_SPEC_STATUS_DATABASE, "execute stops at broken statement") && ok;
    core::Connection closed;
    ok = expectTrue(closed.open(core::literalText("Z:/no/such/dir/x.db")) == KIOTTY_SPEC_STATUS_INPUT_OUTPUT, "open missing dir") && ok;
    ok = expectTrue(closed.message().length() > 0, "open failure message") && ok;
    return ok;
}

bool testSavepointRollback()
{
    core::Connection connection;
    bool ok = expectTrue(openMemory(connection), "open memory database");
    {
        core::Savepoint savepoint(connection);
        ok = expectTrue(savepoint.begin() == KIOTTY_SPEC_STATUS_SUCCESS, "begin savepoint") && ok;
        connection.execute(core::literalText("INSERT INTO t VALUES (1, 'x');"));
    }
    ok = expectTrue(countRows(connection) == 0, "uncommitted savepoint rolls back") && ok;
    {
        core::Savepoint savepoint(connection);
        savepoint.begin();
        connection.execute(core::literalText("INSERT INTO t VALUES (2, 'y');"));
        ok = expectTrue(savepoint.commit() == KIOTTY_SPEC_STATUS_SUCCESS, "commit savepoint") && ok;
    }
    ok = expectTrue(countRows(connection) == 1, "committed savepoint keeps rows") && ok;
    return ok;
}

bool testQueryMove()
{
    core::Connection connection;
    bool ok = expectTrue(openMemory(connection), "open memory database");
    auto first = connection.prepare(core::literalText("SELECT 1"));
    core::Query moved(std::move(first.value));
    ok = expectTrue(moved.step().value, "moved query steps") && ok;
    core::Query assigned;
    assigned = std::move(moved);
    ok = expectTrue(assigned.row().integer(0) == 1, "move assigned query keeps row") && ok;
    return ok;
}

}

int main()
{
    const std::pair<const char*, Test> tests[] = {
        {"text view", testTextView},
        {"bind and read", testBindAndRead},
        {"failures", testFailures},
        {"savepoint rollback", testSavepointRollback},
        {"query move", testQueryMove},
        {"empty text is not null", testEmptyTextIsNotNull},
        {"bind failure is kept", testBindFailureIsKept},
    };

    int failed = 0;
    for (const auto& test : tests) {
        const bool ok = test.second();
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", test.first);
        if (!ok) { ++failed; }
    }
    std::printf("%d failed\n", failed);
    return failed;
}
