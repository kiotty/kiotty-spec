#include "implementation/data/repository/kiotty_meta_store.h"

#include "implementation/core/kiotty_text_view.h"

namespace kiotty::spec::implementation {

namespace {

constexpr int MIN_ID_DIGITS = 3;
constexpr size_t MAX_DIGITS = 10;

KiottySpecStatus appendNumber(entity::IdText& text, uint32_t number)
{
    char digits[MAX_DIGITS] {};
    size_t count = 0;
    do {
        digits[count] = static_cast<char>('0' + number % 10);
        number /= 10;
        ++count;
    } while (number > 0);
    while (count < MIN_ID_DIGITS) { digits[count++] = '0'; }
    for (size_t index = count; index > 0; --index) {
        const KiottySpecStatus status = text.append(Span<const char>(&digits[index - 1], 1));
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

// "DSN-012" → ("DSN", 12). 숫자 꼬리가 없으면 number = 0
void splitId(Span<const char> id, Span<const char>& prefix, uint32_t& number)
{
    size_t dash = id.length();
    for (size_t index = id.length(); index > 0; --index) {
        if (id[index - 1] == '-') {
            dash = index - 1;
            break;
        }
    }
    number = 0;
    prefix = Span<const char>(id.data(), dash);
    if (dash == id.length()) { return; }
    for (size_t index = dash + 1; index < id.length(); ++index) {
        const char letter = id[index];
        if (letter < '0' || letter > '9' || number > 100000000u) {
            number = 0;
            return;
        }
        number = number * 10 + static_cast<uint32_t>(letter - '0');
    }
}

}

KiottySpecStatus MetaStore::set(Span<const char> key, Span<const char> value)
{
    auto query = _storage.prepare(literalText("INSERT OR REPLACE INTO meta (key, value) VALUES (?1, ?2)"));
    if (!query) { return query.code; }
    query.value.bind(1, key);
    query.value.bind(2, value);
    return _storage.check(query.value.execute());
}

Result<MetaValue, KiottySpecStatus> MetaStore::get(Span<const char> key)
{
    using ValueResult = Result<MetaValue, KiottySpecStatus>;
    auto query = _storage.prepare(literalText("SELECT value FROM meta WHERE key = ?1"));
    if (!query) { return ValueResult::failure(query.code); }
    query.value.bind(1, key);
    const auto stepped = query.value.step();
    if (!stepped) { return ValueResult::failure(_storage.check(stepped.code)); }
    if (!stepped.value) { return ValueResult::failure(KIOTTY_SPEC_STATUS_NOT_FOUND); }
    MetaValue value;
    const KiottySpecStatus copied = value.assign(query.value.row().text(0));
    if (copied != KIOTTY_SPEC_STATUS_SUCCESS) { return ValueResult::failure(copied); }
    return ValueResult::success(value);
}

Result<entity::IdText, KiottySpecStatus> MetaStore::nextId(Span<const char> prefix)
{
    using IdResult = Result<entity::IdText, KiottySpecStatus>;
    auto query = _storage.prepare(literalText(
        "INSERT INTO id_counter (prefix, next) VALUES (?1, 2) ON CONFLICT (prefix) DO UPDATE SET next = next + 1 RETURNING next - 1"));
    if (!query) { return IdResult::failure(query.code); }
    query.value.bind(1, prefix);
    const auto stepped = query.value.step();
    if (!stepped) { return IdResult::failure(_storage.check(stepped.code)); }
    const int64_t number = query.value.row().integer(0);
    entity::IdText id;
    KiottySpecStatus status = id.assign(prefix);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = id.append(literalText("-")); }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = appendNumber(id, static_cast<uint32_t>(number)); }
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return IdResult::failure(status); }
    return IdResult::success(id);
}

KiottySpecStatus MetaStore::observeId(Span<const char> id)
{
    Span<const char> prefix;
    uint32_t number = 0;
    splitId(id, prefix, number);
    if (number == 0) { return KIOTTY_SPEC_STATUS_SUCCESS; }
    auto query = _storage.prepare(literalText(
        "INSERT INTO id_counter (prefix, next) VALUES (?1, ?2) ON CONFLICT (prefix) DO UPDATE SET next = MAX(next, ?2)"));
    if (!query) { return query.code; }
    query.value.bind(1, prefix);
    query.value.bind(2, static_cast<int64_t>(number) + 1);
    return _storage.check(query.value.execute());
}

}
