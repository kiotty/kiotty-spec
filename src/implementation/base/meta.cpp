#include "implementation/base/meta.h"

#include "implementation/core/error.h"
#include "implementation/core/text.h"

#include <charconv>

namespace kiotty::base {

std::optional<std::string> Meta::get(std::string_view key) {
    auto stmt = db_.prepare("SELECT value FROM meta WHERE key = ?1");
    stmt.bindAll(key);
    if (!stmt.step()) {
        return std::nullopt;
    }
    return stmt.text(0);
}

void Meta::set(std::string_view key, std::string_view value) {
    db_.prepare("INSERT INTO meta (key, value) VALUES (?1, ?2) ON CONFLICT(key) DO UPDATE SET value = excluded.value")
        .bindAll(key, value)
        .run();
}

std::optional<std::string> Meta::policy(std::string_view key) { return get("policy." + std::string(key)); }

void Meta::setPolicy(std::string_view key, std::string_view value) {
    core::require(!key.empty(), KiottySpecStatus_InvalidArg, "정책 키가 비어 있음");
    set("policy." + std::string(key), value);
}

int64_t Meta::policyInt(std::string_view key, int64_t fallback) {
    const auto value = policy(key);
    if (!value) {
        return fallback;
    }
    int64_t parsed = 0;
    const auto [ptr, ec] = std::from_chars(value->data(), value->data() + value->size(), parsed);
    return ec == std::errc() && ptr == value->data() + value->size() ? parsed : fallback;
}

std::string Meta::nextId(std::string_view prefix, int width) {
    db_.prepare("INSERT INTO id_counter (prefix, next) VALUES (?1, 1) ON CONFLICT(prefix) DO NOTHING")
        .bindAll(prefix)
        .run();
    auto select = db_.prepare("SELECT next FROM id_counter WHERE prefix = ?1");
    select.bindAll(prefix);
    select.step();
    const int64_t number = select.integer(0);
    db_.prepare("UPDATE id_counter SET next = next + 1 WHERE prefix = ?1").bindAll(prefix).run();
    return core::numberedId(prefix, number, width);
}

int64_t Meta::nextWriteSeq() {
    db_.prepare("INSERT INTO id_counter (prefix, next) VALUES ('#wseq', 1) ON CONFLICT(prefix) DO UPDATE SET next = next + 1")
        .run();
    auto select = db_.prepare("SELECT next FROM id_counter WHERE prefix = '#wseq'");
    select.step();
    return select.integer(0);
}

} // namespace kiotty::base
