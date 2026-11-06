#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

namespace kiotty::spec::implementation {

enum class ColumnType { Integer, Float, Text, Blob, Null };

class Statement {
public:
    Statement(sqlite3 *db, std::string_view sql);
    ~Statement();

    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;
    Statement(Statement &&other) noexcept;
    Statement &operator=(Statement &&) = delete;

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
    void reset();

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
    sqlite3 *db_;
    sqlite3_stmt *stmt_;
};

class Database {
public:
    explicit Database(const std::string &utf8Path);
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    void exec(std::string_view sql);
    Statement prepare(std::string_view sql);
    int64_t lastInsertRowId() const;
    int64_t changes() const;

private:
    sqlite3 *db_ = nullptr;
};

class Savepoint {
public:
    explicit Savepoint(Database &db);
    ~Savepoint();

    Savepoint(const Savepoint &) = delete;
    Savepoint &operator=(const Savepoint &) = delete;

    void commit();

private:
    Database &db_;
    bool done_ = false;
};

}
