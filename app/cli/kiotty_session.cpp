#include "kiotty_session.h"

#include "src/infra/kiotty_path_utf8.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace kiotty::cli {

namespace {

constexpr std::string_view FLAG_NAMES[] = {"tags", "relations", "up", "down", "help"};
constexpr size_t OPTION_PREFIX_LENGTH = 2;
constexpr size_t CHECKPOINT_DIGIT_WIDTH = 4;
constexpr uint32_t CHECKPOINT_NUMBER_MASK = 0xFFFFFFFFu;
constexpr size_t CHECKPOINT_PREFIX_LENGTH = 3;
constexpr size_t MAX_CHECKPOINT_DIGITS = 9;
constexpr size_t MAX_NUMBER_DIGITS = 10;
constexpr unsigned EIGHT_FOLD_SHIFT = 3;
constexpr std::string_view DIGITS = "0123456789";

bool isFlag(std::string_view name, const std::vector<std::string_view>& extra_flags);
std::vector<std::string> split(std::string_view text, char separator);
std::string listNames(const std::string& names);
uint64_t findBits(const KiottySpecTagDefinition& definition, std::string_view name);
std::string listKnownBitNames(const KiottySpecTagDefinition& definition);

}

std::optional<std::string> Arguments::option(std::string_view name) const
{
    std::optional<std::string> found {};
    for (const auto& [key, value] : options) {
        if (key == name) { found = value; }
    }
    return found;
}

std::vector<std::string> Arguments::all(std::string_view name) const
{
    std::vector<std::string> values {};
    for (const auto& [key, value] : options) {
        if (key == name) { values.push_back(value); }
    }
    return values;
}

bool Arguments::flag(std::string_view name) const
{
    for (const auto& value : flags) {
        if (value == name) { return true; }
    }
    return false;
}

Arguments parseArguments(const std::vector<std::string>& tokens, size_t first,
                         const std::vector<std::string_view>& extra_flags)
{
    Arguments arguments {};
    const size_t count = tokens.size();
    for (size_t index = first; index < count; ++index) {
        const std::string& token = tokens[index];
        const bool is_option = token.size() > OPTION_PREFIX_LENGTH &&
                               token.compare(0, OPTION_PREFIX_LENGTH, "--") == 0;
        if (!is_option) {
            arguments.positional.push_back(token);
            continue;
        }
        const std::string name = token.substr(OPTION_PREFIX_LENGTH);
        const bool is_flag = isFlag(name, extra_flags) || index + 1 >= count;
        if (is_flag) {
            arguments.flags.push_back(name);
            continue;
        }
        ++index;
        arguments.options.emplace_back(name, tokens[index]);
    }
    return arguments;
}

int usage(std::string_view message)
{
    std::cerr << "error: usage: " << message << "\n";
    return USAGE_EXIT_CODE;
}

std::optional<KiottySpecItemKind> kindArgument(const Session& session, std::string_view text)
{
    const KiottySpecItemKind kind = KiottySpec_parseKind(textOf(text));
    if (kind != 0) { return kind; }
    std::string names {};
    for (uint32_t value = 1;; ++value) {
        const KiottySpecItemKind candidate = static_cast<KiottySpecItemKind>(value);
        const bool has_kind = !kindName(candidate).empty();
        if (!has_kind) { break; }
        const KiottySpecModelResult model = KiottySpec_readModel(&session.context, candidate);
        if (model.code != KIOTTY_SPEC_STATUS_SUCCESS) { continue; }
        if (text == model.value.name) { return candidate; }
        names += (names.empty() ? "" : ", ") + kindName(candidate) + "(" + model.value.name + ")";
    }
    std::cerr << "error: " << formatMessage(MessageKey::UNKNOWN_KIND, {text, names}) << "\n";
    return std::nullopt;
}

std::optional<TagColumn> findTag(const Session& session, KiottySpecItemKind kind,
                                 std::string_view name)
{
    const std::vector<TagColumn> tags = readTagDefinitions(session, kind);
    std::string names {};
    for (const TagColumn& column : tags) {
        if (column.definition.name == name) { return column; }
        names += (names.empty() ? "" : ", ") + std::string(column.definition.name);
    }
    std::cerr << "error: "
              << formatMessage(MessageKey::UNKNOWN_TAG, {kindName(kind), name, listNames(names)})
              << "\n";
    return std::nullopt;
}

std::optional<uint32_t> findRelation(const Session& session, KiottySpecItemKind kind,
                                     std::string_view name)
{
    const std::vector<KiottySpecRelationDefinition> relations =
        readRelationDefinitions(session, kind);
    std::string names {};
    for (const KiottySpecRelationDefinition& definition : relations) {
        if (definition.name == name) { return definition.index; }
        names += (names.empty() ? "" : ", ") + std::string(definition.name);
    }
    std::cerr << "error: "
              << formatMessage(MessageKey::UNKNOWN_RELATION,
                               {kindName(kind), name, listNames(names)})
              << "\n";
    return std::nullopt;
}

KiottySpecText textOf(std::string_view text) { return KiottySpecText{text.data(), text.size()}; }

std::string kindName(KiottySpecItemKind kind) { return KiottySpec_formatKind(kind).value.value; }

std::string stateName(KiottySpecCheckpointState state)
{ return KiottySpec_formatCheckpointState(state).value.value; }

std::string quote(std::string_view text)
{
    std::string value = "\"";
    for (const char letter : escape(text)) {
        if (letter == '"') { value += '\\'; }
        value += letter;
    }
    return value + "\"";
}

std::string escape(std::string_view text)
{
    std::string value = cell(text);
    return text.empty() ? std::string() : value;
}

std::string cell(std::string_view value)
{
    std::string text {};
    text.reserve(value.size());
    for (const char letter : value) {
        if (letter == '\t') {
            text += ' ';
        } else if (letter == '\n') {
            text += "\\n";
        } else if (letter != '\r') {
            text += letter;
        }
    }
    return text.empty() ? "-" : text;
}

void printRow(const std::vector<std::string>& cells)
{
    std::string line {};
    const size_t count = cells.size();
    for (size_t index = 0; index < count; ++index) {
        if (index > 0) { line += '\t'; }
        line += cells[index];
    }
    line += '\n';
    std::fwrite(line.data(), 1, line.size(), stdout);
}

std::string checkpointName(KiottySpecCheckpointHandle handle)
{
    if (handle == 0) { return "-"; }
    const std::string digits = std::to_string(handle & CHECKPOINT_NUMBER_MASK);
    const bool is_short = digits.size() < CHECKPOINT_DIGIT_WIDTH;
    const size_t padding = is_short ? CHECKPOINT_DIGIT_WIDTH - digits.size() : 0;
    return "CP-" + std::string(padding, '0') + digits;
}

std::optional<KiottySpecCheckpointHandle> parseCheckpoint(std::string_view text)
{
    const std::string_view prefix = text.substr(0, CHECKPOINT_PREFIX_LENGTH);
    const bool has_prefix =
        text.size() > CHECKPOINT_PREFIX_LENGTH && (prefix == "CP-" || prefix == "cp-");
    if (has_prefix) { text.remove_prefix(CHECKPOINT_PREFIX_LENGTH); }
    const bool is_wrong_length = text.empty() || text.size() > MAX_CHECKPOINT_DIGITS;
    if (is_wrong_length) { return std::nullopt; }
    const std::optional<uint32_t> number = parseNumber(text);
    const bool is_valid = number.has_value() && *number != 0;
    if (!is_valid) { return std::nullopt; }
    return KiottySpecCheckpointHandle {*number};
}

std::optional<uint32_t> parseNumber(std::string_view text)
{
    const size_t first_other = text.find_first_not_of(DIGITS);
    const bool is_digits = !text.empty() && first_other == std::string_view::npos;
    const bool is_short = text.size() <= MAX_NUMBER_DIGITS;
    const bool is_number = is_digits && is_short;
    if (!is_number) { return std::nullopt; }
    uint64_t value = 0;
    for (const char letter : text) {
        value = (value << EIGHT_FOLD_SHIFT) + (value << 1) + static_cast<uint64_t>(letter - '0');
    }
    const bool is_in_range = value <= UINT32_MAX;
    if (!is_in_range) { return std::nullopt; }
    return static_cast<uint32_t>(value);
}

std::optional<std::string> readFileText(const std::string& path)
{
    std::ifstream stream(pathFromUtf8(path), std::ios::binary);
    if (!stream) { return std::nullopt; }
    std::ostringstream content {};
    content << stream.rdbuf();
    return content.str();
}

std::string itemId(const Session& session, KiottySpecItemHandle handle)
{
    if (handle == KIOTTY_SPEC_INVALID_ITEM_HANDLE) { return "-"; }
    const KiottySpecItemResult item = KiottySpec_readItemByHandle(&session.context, handle);
    if (item.code != KIOTTY_SPEC_STATUS_SUCCESS) { return "-"; }
    return item.value.id;
}

std::optional<uint64_t> parseBits(const KiottySpecTagDefinition& definition, std::string_view names)
{
    if (names == "-") { return uint64_t{0}; }
    uint64_t value = 0;
    for (const std::string& name : split(names, ',')) {
        const uint64_t bits = findBits(definition, name);
        if (bits == 0) {
            const std::string known = listKnownBitNames(definition);
            std::cerr << "error: "
                      << formatMessage(MessageKey::UNKNOWN_TAG_VALUE,
                                       {definition.name, name, known})
                      << "\n";
            return std::nullopt;
        }
        value |= bits;
    }
    return value;
}

std::string bitNames(const KiottySpecTagDefinition& definition, uint64_t value)
{
    std::string text {};
    for (size_t bit = 0; bit < KIOTTY_SPEC_TAG_BIT_COUNT; ++bit) {
        if ((value & (uint64_t{1} << bit)) == 0) { continue; }
        const bool has_text = !text.empty();
        if (has_text) { text += '|'; }
        text +=
            definition.bit_names[bit][0] != '\0' ? definition.bit_names[bit] : std::to_string(bit);
    }
    return text.empty() ? "-" : text;
}

bool detectColorTerminal()
{
#ifdef _WIN32
    const bool is_terminal = _isatty(_fileno(stdout)) != 0;
    if (!is_terminal) { return false; }
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (output == INVALID_HANDLE_VALUE) { return false; }
    const bool has_console_mode = GetConsoleMode(output, &mode) != 0;
    if (!has_console_mode) { return false; }
    return SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

namespace {

bool isFlag(std::string_view name, const std::vector<std::string_view>& extra_flags)
{
    for (const std::string_view flag : FLAG_NAMES) {
        if (flag == name) { return true; }
    }
    for (const std::string_view flag : extra_flags) {
        if (flag == name) { return true; }
    }
    return false;
}

uint64_t findBits(const KiottySpecTagDefinition& definition, std::string_view name)
{
    uint64_t value = 0;
    for (size_t bit = 0; bit < KIOTTY_SPEC_TAG_BIT_COUNT; ++bit) {
        const bool is_match =
            definition.bit_names[bit][0] != '\0' && name == definition.bit_names[bit];
        if (is_match) { value |= uint64_t{1} << bit; }
    }
    return value;
}

std::string listKnownBitNames(const KiottySpecTagDefinition& definition)
{
    std::string known {};
    for (size_t bit = 0; bit < KIOTTY_SPEC_TAG_BIT_COUNT; ++bit) {
        const bool is_named = definition.bit_names[bit][0] != '\0';
        if (is_named) {
            known += (known.empty() ? "" : ", ") + std::string(definition.bit_names[bit]);
        }
    }
    return known;
}

std::string listNames(const std::string& names)
{
    const bool has_names = !names.empty();
    return has_names ? names : std::string(findMessage(MessageKey::NOTHING_LISTED));
}

std::vector<std::string> split(std::string_view text, char separator)
{
    std::vector<std::string> parts {};
    size_t start = 0;
    const size_t length = text.size();
    while (start <= length) {
        size_t end = text.find(separator, start);
        if (end == std::string_view::npos) { end = text.size(); }
        parts.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

}

}
