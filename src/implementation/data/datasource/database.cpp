#include "implementation/data/datasource/database.h"

#include "implementation/core/error.h"

#include <utility>

namespace kiotty::spec::implementation {

namespace {

Span<const char> spanOf(std::string_view text) { return Span<const char>(text.data(), text.size()); }

std::string stringOf(Span<const char> text) {
    return text.data() != nullptr ? std::string(text.data(), text.length()) : std::string();
}

[[noreturn]] void failWith(const Connection &connection, KiottySpecStatus status, std::string_view what) {
    std::string message(what);
    message += ": ";
    message += stringOf(connection.message());
    fail(status, message);
}

}

Statement::Statement(Connection &connection, Query query) : connection_(connection), query_(std::move(query)) {}

void Statement::check(KiottySpecStatus status, std::string_view what) const {
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        failWith(connection_, status, what);
    }
}

Statement &Statement::bindOne(int index, int64_t value) {
    check(query_.bind(index, value), "bind int");
    return *this;
}

Statement &Statement::bindOne(int index, double value) {
    check(query_.bind(index, value), "bind double");
    return *this;
}

Statement &Statement::bindOne(int index, std::string_view value) {
    check(query_.bind(index, spanOf(value)), "bind text");
    return *this;
}

Statement &Statement::bindOne(int index, const char *value) {
    if (value == nullptr) {
        return bindOne(index, nullptr);
    }
    return bindOne(index, std::string_view(value));
}

Statement &Statement::bindOne(int index, std::nullptr_t) {
    check(query_.bind(index, nullptr), "bind null");
    return *this;
}

Statement &Statement::bindOne(int index, const std::optional<std::string> &value) {
    return value ? bindOne(index, std::string_view(*value)) : bindOne(index, nullptr);
}

Statement &Statement::bindOne(int index, const std::optional<int64_t> &value) {
    return value ? bindOne(index, *value) : bindOne(index, nullptr);
}

bool Statement::step() {
    const Result<bool, KiottySpecStatus> stepped = query_.step();
    check(stepped.code, "step");
    return stepped.value;
}

void Statement::run() { check(query_.run(), "step"); }

int Statement::columnCount() const { return query_.row().count(); }

std::string Statement::columnName(int column) const { return stringOf(query_.row().name(column)); }

ColumnType Statement::columnType(int column) const { return query_.row().type(column); }

bool Statement::isNull(int column) const { return query_.row().isNull(column); }

int64_t Statement::integer(int column) const { return query_.row().integer(column); }

double Statement::real(int column) const { return query_.row().real(column); }

std::string Statement::text(int column) const { return stringOf(query_.row().text(column)); }

std::optional<std::string> Statement::optText(int column) const {
    if (isNull(column)) {
        return std::nullopt;
    }
    return text(column);
}

std::optional<int64_t> Statement::optInteger(int column) const {
    if (isNull(column)) {
        return std::nullopt;
    }
    return integer(column);
}

bool Statement::readOnly() const { return query_.isReadOnly(); }

Database::Database(const std::string &utf8Path) {
    const KiottySpecStatus opened = connection_.open(spanOf(utf8Path));
    if (opened == KIOTTY_SPEC_STATUS_INPUT_OUTPUT || opened == KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED) {
        failWith(connection_, KIOTTY_SPEC_STATUS_INPUT_OUTPUT, "cannot open database " + utf8Path);
    }
    if (opened != KIOTTY_SPEC_STATUS_SUCCESS) {
        failWith(connection_, opened, "exec failed");
    }
}

void Database::exec(std::string_view sql) {
    const KiottySpecStatus status = connection_.execute(spanOf(sql));
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        failWith(connection_, status, "exec failed");
    }
}

Statement Database::prepare(std::string_view sql) {
    Result<Query, KiottySpecStatus> prepared = connection_.prepare(spanOf(sql));
    if (!prepared) {
        failWith(connection_, prepared.code, std::string("prepare failed [") + std::string(sql) + "]");
    }
    return Statement(connection_, std::move(prepared.value));
}

int64_t Database::lastInsertRowId() const { return connection_.insertedRowId(); }

int64_t Database::changes() const { return connection_.changedRowCount(); }

LegacySavepoint::LegacySavepoint(Database &db) : db_(db), savepoint_(db.connection()) {
    const KiottySpecStatus status = savepoint_.begin();
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        failWith(db_.connection(), status, "exec failed");
    }
}

void LegacySavepoint::commit() {
    const KiottySpecStatus status = savepoint_.commit();
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) {
        failWith(db_.connection(), status, "exec failed");
    }
}

}
