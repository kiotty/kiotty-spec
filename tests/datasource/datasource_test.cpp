#include "implementation/core/kiotty_text_view.h"
#include "implementation/data/datasource/kiotty_db_connection.h"
#include "implementation/data/datasource/kiotty_file_reader.h"
#include "implementation/data/datasource/kiotty_file_system.h"
#include "implementation/data/datasource/kiotty_file_writer.h"
#include "implementation/data/datasource/kiotty_savepoint.h"
#include "implementation/data/datasource/kiotty_statement.h"

#include <cstdio>
#include <string_view>
#include <utility>

namespace core = kiotty::spec::implementation;

namespace {

using Test = bool (*)();

bool expectTrue(bool condition, const char* label)
{
    if (!condition) { std::printf("  FAIL: %s\n", label); }
    return condition;
}

bool openMemory(core::DBConnection& connection)
{
    const bool opened = connection.open(core::literalText(":memory:")) == KIOTTY_SPEC_STATUS_SUCCESS;
    const bool created = connection.executeScript(core::literalText("CREATE TABLE t (a INTEGER, b TEXT);")) == KIOTTY_SPEC_STATUS_SUCCESS;
    return opened && created;
}

int64_t countRows(core::DBConnection& connection)
{
    core::Statement prepared(connection, core::literalText("SELECT COUNT(*) FROM t"));
    prepared.step();
    return prepared.row().integer(0);
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
    core::DBConnection connection;
    bool ok = expectTrue(openMemory(connection), "open memory database");
    core::Statement insert(connection, core::literalText("INSERT INTO t VALUES (?1, ?2)"));
    ok = expectTrue(insert.status() == KIOTTY_SPEC_STATUS_SUCCESS, "prepare insert") && ok;
    ok = expectTrue(insert.bind(1, static_cast<int64_t>(7)) == KIOTTY_SPEC_STATUS_SUCCESS, "bind integer") && ok;
    ok = expectTrue(insert.bind(2, core::literalText("\xED\x95\x9C")) == KIOTTY_SPEC_STATUS_SUCCESS, "bind text") && ok;
    ok = expectTrue(insert.execute() == KIOTTY_SPEC_STATUS_SUCCESS, "run insert") && ok;
    ok = expectTrue(insert.changedRowCount() == 1 && insert.insertedRowId() == 1, "insert counters") && ok;
    core::Statement select(connection, core::literalText("SELECT a, b, NULL FROM t"));
    const auto stepped = select.step();
    const core::Row row = select.row();
    ok = expectTrue(stepped.code == KIOTTY_SPEC_STATUS_SUCCESS && stepped.value, "step yields row") && ok;
    ok = expectTrue(row.integer(0) == 7 && row.text(1).length() == 3, "row values") && ok;
    ok = expectTrue(row.type(0) == core::ColumnType::INTEGER && row.isNull(2), "row types") && ok;
    ok = expectTrue(row.count() == 3 && row.name(1).length() == 1, "row shape") && ok;
    return ok;
}

bool testEmptyTextIsNotNull()
{
    core::DBConnection connection;
    bool ok = expectTrue(openMemory(connection), "open memory database");
    core::Statement query(connection, core::literalText("SELECT ?1 = '', ?1 IS NULL"));
    query.bind(1, core::Span<const char>());
    const auto stepped = query.step();
    ok = expectTrue(stepped.value && query.row().integer(0) == 1, "empty span binds empty text") && ok;
    ok = expectTrue(query.row().integer(1) == 0, "empty span is not null") && ok;
    return ok;
}

bool testBindFailureIsKept()
{
    core::DBConnection connection;
    bool ok = expectTrue(openMemory(connection), "open memory database");
    core::Statement query(connection, core::literalText("SELECT ?1"));
    ok = expectTrue(query.bind(5, static_cast<int64_t>(1)) == KIOTTY_SPEC_STATUS_DATABASE, "out of range bind fails") && ok;
    query.bind(1, static_cast<int64_t>(1));
    ok = expectTrue(query.step().code == KIOTTY_SPEC_STATUS_DATABASE, "step reports earlier bind failure") && ok;
    ok = expectTrue(query.execute() == KIOTTY_SPEC_STATUS_DATABASE, "run reports earlier bind failure") && ok;
    return ok;
}

bool testFailures()
{
    core::DBConnection connection;
    bool ok = expectTrue(openMemory(connection), "open memory database");
    core::Statement bad(connection, core::literalText("SELECT nope FROM nowhere"));
    ok = expectTrue(bad.status() == KIOTTY_SPEC_STATUS_DATABASE, "bad sql fails to prepare") && ok;
    ok = expectTrue(connection.message().length() > 0, "failure leaves message") && ok;
    const KiottySpecStatus status = connection.executeScript(core::literalText("SELECT 1; BROKEN;"));
    ok = expectTrue(status == KIOTTY_SPEC_STATUS_DATABASE, "execute stops at broken statement") && ok;
    core::DBConnection closed;
    ok = expectTrue(closed.open(core::literalText("Z:/no/such/dir/x.db")) == KIOTTY_SPEC_STATUS_INPUT_OUTPUT, "open missing dir") && ok;
    ok = expectTrue(closed.message().length() > 0, "open failure message") && ok;
    return ok;
}

bool testSavepointRollback()
{
    core::DBConnection connection;
    bool ok = expectTrue(openMemory(connection), "open memory database");
    {
        core::Savepoint savepoint(connection);
        ok = expectTrue(savepoint.begin() == KIOTTY_SPEC_STATUS_SUCCESS, "begin savepoint") && ok;
        connection.executeScript(core::literalText("INSERT INTO t VALUES (1, 'x');"));
    }
    ok = expectTrue(countRows(connection) == 0, "uncommitted savepoint rolls back") && ok;
    {
        core::Savepoint savepoint(connection);
        savepoint.begin();
        connection.executeScript(core::literalText("INSERT INTO t VALUES (2, 'y');"));
        ok = expectTrue(savepoint.commit() == KIOTTY_SPEC_STATUS_SUCCESS, "commit savepoint") && ok;
    }
    ok = expectTrue(countRows(connection) == 1, "committed savepoint keeps rows") && ok;
    return ok;
}

bool testStatementMove()
{
    core::DBConnection connection;
    bool ok = expectTrue(openMemory(connection), "open memory database");
    core::Statement first(connection, core::literalText("SELECT 1"));
    core::Statement moved(std::move(first));
    ok = expectTrue(moved.step().value, "moved statement steps") && ok;
    core::Statement assigned;
    assigned = std::move(moved);
    ok = expectTrue(assigned.row().integer(0) == 1, "move assigned statement keeps row") && ok;
    return ok;
}


bool testFileReaderLines()
{
    const core::Span<const char> path = core::literalText("kiotty_file_reader_test.txt");
    bool ok = true;
    {
        core::FileWriter writer;
        ok = expectTrue(writer.open(path) == KIOTTY_SPEC_STATUS_SUCCESS, "open writer") && ok;
        writer.write(core::literalText("first\r\n\nsecond  \n"));
        writer.write(core::literalText("third-without-newline"));
    }
    {
        char buffer[24] {};
        core::FileReader reader(core::Span<char>(buffer, sizeof(buffer)));
        ok = expectTrue(reader.open(path) == KIOTTY_SPEC_STATUS_SUCCESS, "open reader") && ok;
        const char* const expected[] = {"first", "", "second", "third-without-newline"};
        for (const char* line : expected) {
            const auto next = reader.next();
            const std::string_view got(reader.line().data(), reader.line().length());
            ok = expectTrue(next && next.value && got == line, line) && ok;
        }
        const auto end = reader.next();
        ok = expectTrue(end && !end.value, "end of file") && ok;

        char small[8] {};
        core::FileReader tight(core::Span<char>(small, sizeof(small)));
        tight.open(path);
        tight.next();
        tight.next();
        const auto too_long = tight.next();
        ok = expectTrue(too_long.code == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED, "line longer than buffer") && ok;

        core::FileReader missing(core::Span<char>(buffer, sizeof(buffer)));
        ok = expectTrue(missing.open(core::literalText("no_such_kiotty_file.txt")) == KIOTTY_SPEC_STATUS_NOT_FOUND, "missing file") && ok;
    }
    // 열린 파일은 지울 수 없으므로 리더를 닫은 뒤에 지운다.
    core::removeFile(path);
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
        {"statement move", testStatementMove},
        {"empty text is not null", testEmptyTextIsNotNull},
        {"bind failure is kept", testBindFailureIsKept},
        {"file reader lines", testFileReaderLines},
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
