#pragma once

#include "implementation/data/datasource/column_type.h"
#include "implementation/data/datasource/connection.h"
#include "implementation/data/datasource/query.h"
#include "implementation/data/datasource/savepoint.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kiotty::spec::implementation {

class Statement {
public:
    Statement(Connection &connection, Query query);

    template <class... Args>
    Statement &bindAll(const Args &...args) {
        int index = 1;
        (bindOne(index++, args), ...);
        return *this;
    }

    Statement &bindOne(int index, int64_t value);
    Statement &bindOne(int index, int32_t value) { return bindOne(index, static_cast<int64_t>(value)); }
    Statement &bindOne(int index, bool value) { return bindOne(index, static_cast<int64_t>(value ? 1 : 0)); }
    Statement &bindOne(int index, double value);
    Statement &bindOne(int index, std::string_view value);
    Statement &bindOne(int index, const std::string &value) { return bindOne(index, std::string_view(value)); }
    Statement &bindOne(int index, const char *value);
    Statement &bindOne(int index, std::nullptr_t);
    Statement &bindOne(int index, const std::optional<std::string> &value);
    Statement &bindOne(int index, const std::optional<int64_t> &value);

    bool step();
    void run();

    int columnCount() const;
    std::string columnName(int column) const;
    ColumnType columnType(int column) const;
    bool isNull(int column) const;
    int64_t integer(int column) const;
    double real(int column) const;
    std::string text(int column) const;
    std::optional<std::string> optText(int column) const;
    std::optional<int64_t> optInteger(int column) const;
    bool readOnly() const;

private:
    void check(KiottySpecStatus status, std::string_view what) const;

    Connection &connection_;
    Query query_;
};

class Database {
public:
    Database(Connection &connection, const std::string &utf8Path);

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    void exec(std::string_view sql);
    Statement prepare(std::string_view sql);
    int64_t lastInsertRowId() const;
    int64_t changes() const;
    Connection &connection() { return connection_; }

private:
    Connection &connection_;
};

class LegacySavepoint {
public:
    explicit LegacySavepoint(Database &db);

    void commit();

private:
    Database &db_;
    Savepoint savepoint_;
};

}
