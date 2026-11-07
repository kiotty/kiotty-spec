#include "implementation/data/repository/meta.h"

#include "implementation/core/text.h"
#include "implementation/core/text_view.h"

#include <charconv>

namespace kiotty::spec::implementation {

namespace {

constexpr char SELECT_SQL[] = "SELECT value FROM meta WHERE key = ?1";
constexpr char UPSERT_SQL[] =
    "INSERT INTO meta (key, value) VALUES (?1, ?2) ON CONFLICT(key) DO UPDATE SET value = excluded.value";
constexpr char SEED_COUNTER_SQL[] = "INSERT INTO id_counter (prefix, next) VALUES (?1, 1) ON CONFLICT(prefix) DO NOTHING";
constexpr char SELECT_COUNTER_SQL[] = "SELECT next FROM id_counter WHERE prefix = ?1";
constexpr char ADVANCE_COUNTER_SQL[] = "UPDATE id_counter SET next = next + 1 WHERE prefix = ?1";
constexpr char ADVANCE_WRITE_SQL[] =
    "INSERT INTO id_counter (prefix, next) VALUES ('#wseq', 1) ON CONFLICT(prefix) DO UPDATE SET next = next + 1";
constexpr char WRITE_PREFIX[] = "#wseq";
constexpr char POLICY_PREFIX[] = "policy.";

std::string policyKey(std::string_view key) { return std::string(POLICY_PREFIX) + std::string(key); }

int64_t parseOr(std::string_view text, int64_t fallback)
{
    int64_t parsed = 0;
    const char* const end = text.data() + text.size();
    const auto [position, error] = std::from_chars(text.data(), end, parsed);
    const bool isWhole = error == std::errc() && position == end;
    return isWhole ? parsed : fallback;
}

}

OptionalTextResult Meta::get(std::string_view key)
{
    auto query = _storage.prepare(literalText(SELECT_SQL));
    if (!query) { return OptionalTextResult::failure(query.code); }
    query.value.bind(1, spanOf(key));
    const auto stepped = query.value.step();
    if (!stepped) { return OptionalTextResult::failure(_storage.check(stepped.code)); }
    if (!stepped.value) { return OptionalTextResult::success(std::nullopt); }
    return OptionalTextResult::success(std::string(viewOf(query.value.row().text(0))));
}

KiottySpecStatus Meta::set(std::string_view key, std::string_view value)
{
    auto query = _storage.prepare(literalText(UPSERT_SQL));
    if (!query) { return query.code; }
    query.value.bind(1, spanOf(key));
    query.value.bind(2, spanOf(value));
    return _storage.check(query.value.run());
}

OptionalTextResult Meta::policy(std::string_view key) { return get(policyKey(key)); }

KiottySpecStatus Meta::setPolicy(std::string_view key, std::string_view value)
{
    if (key.empty()) { return _storage.reject(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT, {literalText("정책 키가 비어 있음")}); }
    return set(policyKey(key), value);
}

SequenceResult Meta::policyInt(std::string_view key, int64_t fallback)
{
    const auto value = policy(key);
    if (!value) { return SequenceResult::failure(value.code); }
    if (!value.value) { return SequenceResult::success(fallback); }
    return SequenceResult::success(parseOr(*value.value, fallback));
}

TextResult Meta::nextId(std::string_view prefix, int width)
{
    const auto number = counter(prefix);
    if (!number) { return TextResult::failure(number.code); }
    return TextResult::success(numberedId(prefix, number.value, width));
}

SequenceResult Meta::nextWriteSeq()
{
    auto advance = _storage.prepare(literalText(ADVANCE_WRITE_SQL));
    if (!advance) { return SequenceResult::failure(advance.code); }
    const KiottySpecStatus advanced = _storage.check(advance.value.run());
    if (advanced != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(advanced); }
    auto select = _storage.prepare(literalText(SELECT_COUNTER_SQL));
    if (!select) { return SequenceResult::failure(select.code); }
    select.value.bind(1, literalText(WRITE_PREFIX));
    const auto stepped = select.value.step();
    if (!stepped) { return SequenceResult::failure(_storage.check(stepped.code)); }
    return SequenceResult::success(select.value.row().integer(0));
}

SequenceResult Meta::counter(std::string_view prefix)
{
    auto seed = _storage.prepare(literalText(SEED_COUNTER_SQL));
    if (!seed) { return SequenceResult::failure(seed.code); }
    seed.value.bind(1, spanOf(prefix));
    const KiottySpecStatus seeded = _storage.check(seed.value.run());
    if (seeded != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(seeded); }
    auto select = _storage.prepare(literalText(SELECT_COUNTER_SQL));
    if (!select) { return SequenceResult::failure(select.code); }
    select.value.bind(1, spanOf(prefix));
    const auto stepped = select.value.step();
    if (!stepped) { return SequenceResult::failure(_storage.check(stepped.code)); }
    const int64_t number = select.value.row().integer(0);
    auto advance = _storage.prepare(literalText(ADVANCE_COUNTER_SQL));
    if (!advance) { return SequenceResult::failure(advance.code); }
    advance.value.bind(1, spanOf(prefix));
    const KiottySpecStatus advanced = _storage.check(advance.value.run());
    if (advanced != KIOTTY_SPEC_STATUS_SUCCESS) { return SequenceResult::failure(advanced); }
    return SequenceResult::success(number);
}

}
