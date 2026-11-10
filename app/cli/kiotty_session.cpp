#include "kiotty_session.h"

#include "kiotty_path_utf8.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

namespace kiotty::cli {

namespace {

constexpr std::string_view FLAG_NAMES[] = {"hooks", "tags", "relations", "up", "down", "help"};
constexpr size_t ERROR_BUFFER_LENGTH = 4096;

bool isFlag(std::string_view name)
{
    for (const std::string_view flag : FLAG_NAMES) {
        if (flag == name) { return true; }
    }
    return false;
}

std::vector<std::string> split(std::string_view text, char separator)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(separator, start);
        if (end == std::string_view::npos) { end = text.size(); }
        parts.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

}

std::optional<std::string> Arguments::option(std::string_view name) const
{
    std::optional<std::string> found;
    for (const auto& [key, value] : options) {
        if (key == name) { found = value; }
    }
    return found;
}

std::vector<std::string> Arguments::all(std::string_view name) const
{
    std::vector<std::string> values;
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

Arguments parseArguments(const std::vector<std::string>& tokens, size_t first, const std::vector<std::string_view>& extra_flags)
{
    Arguments args;
    for (size_t index = first; index < tokens.size(); ++index) {
        const std::string& token = tokens[index];
        if (token.size() > 2 && token.compare(0, 2, "--") == 0) {
            const std::string name = token.substr(2);
            bool extra = false;
            for (const std::string_view flag : extra_flags) { extra = extra || flag == name; }
            if (isFlag(name) || extra || index + 1 >= tokens.size()) {
                args.flags.push_back(name);
            } else {
                args.options.emplace_back(name, tokens[++index]);
            }
            continue;
        }
        args.positional.push_back(token);
    }
    return args;
}

int fail(Session& session, KiottySpecStatus status)
{
    char message[ERROR_BUFFER_LENGTH] {};
    if (session.context != nullptr) { KiottySpec_readLastError(session.context, message, sizeof(message)); }
    std::cerr << "error: " << KiottySpec_statusName(status);
    if (message[0] != '\0') { std::cerr << ": " << message; }
    std::cerr << "\n";
    return 1;
}

int usage(const std::string& message)
{
    std::cerr << "error: usage: " << message << "\n";
    return 2;
}

KiottySpecText textOf(std::string_view text) { return KiottySpecText{text.data(), text.size()}; }

// 설계서 v1 12 #9. 탭은 공백, 줄바꿈은 \n 두 글자
std::string cell(std::string_view value)
{
    std::string text;
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
    std::string line;
    for (size_t index = 0; index < cells.size(); ++index) {
        if (index > 0) { line += '\t'; }
        line += cells[index];
    }
    line += '\n';
    std::fwrite(line.data(), 1, line.size(), stdout);
}

std::string checkpointName(KiottySpecCheckpointHandle handle)
{
    if (handle == 0) { return "-"; }
    char text[32] {};
    std::snprintf(text, sizeof(text), "CP-%04llu", static_cast<unsigned long long>(handle & 0xFFFFFFFFu));
    return text;
}

std::optional<KiottySpecCheckpointHandle> parseCheckpoint(std::string_view text)
{
    if (text.size() > 3 && (text.substr(0, 3) == "CP-" || text.substr(0, 3) == "cp-")) { text.remove_prefix(3); }
    if (text.empty() || text.size() > 9) { return std::nullopt; }
    uint64_t value = 0;
    for (const char letter : text) {
        if (letter < '0' || letter > '9') { return std::nullopt; }
        value = value * 10 + static_cast<uint64_t>(letter - '0');
    }
    if (value == 0) { return std::nullopt; }
    return value;
}

std::optional<std::string> readFileText(const std::string& path)
{
    std::ifstream stream(pathFromUtf8(path), std::ios::binary);
    if (!stream) { return std::nullopt; }
    std::ostringstream content;
    content << stream.rdbuf();
    return content.str();
}

std::optional<KiottySpecItemKind> kindArgument(Session& session, std::string_view text)
{
    const KiottySpecItemKind kind = KiottySpec_parseKind(textOf(text));
    if (kind != 0) { return kind; }
    std::string names;
    for (uint32_t value = 1; KiottySpec_kindName(static_cast<KiottySpecItemKind>(value))[0] != '\0'; ++value) {
        const KiottySpecItemKind candidate = static_cast<KiottySpecItemKind>(value);
        const KiottySpecModelResult model = KiottySpec_readModel(session.context, candidate);
        if (model.code != KIOTTY_SPEC_STATUS_SUCCESS) { continue; }
        if (text == model.value.name) { return candidate; }
        names += (names.empty() ? "" : ", ") + std::string(KiottySpec_kindName(candidate)) + "(" + model.value.name + ")";
    }
    std::cerr << "error: 알 수 없는 Kind '" << text << "'. 받는 값: " << names << "\n";
    return std::nullopt;
}

std::string itemId(Session& session, KiottySpecItemHandle handle)
{
    if (handle == KIOTTY_SPEC_INVALID_ITEM_HANDLE) { return "-"; }
    const KiottySpecItemResult item = KiottySpec_readItemByHandle(session.context, handle);
    if (item.code != KIOTTY_SPEC_STATUS_SUCCESS) { return "-"; }
    return item.value.id;
}

std::optional<TagColumn> findTag(Session& session, KiottySpecItemKind kind, std::string_view name)
{
    const KiottySpecModelResult model = KiottySpec_readModel(session.context, kind);
    std::string names;
    for (uint32_t index = 0; model.code == KIOTTY_SPEC_STATUS_SUCCESS && index < model.value.tag_count; ++index) {
        TagColumn column {index, {}};
        if (KiottySpec_readTagDefinition(session.context, kind, index, &column.definition).code != KIOTTY_SPEC_STATUS_SUCCESS) { continue; }
        if (column.definition.name == name) { return column; }
        names += (names.empty() ? "" : ", ") + std::string(column.definition.name);
    }
    std::cerr << "error: " << KiottySpec_kindName(kind) << " 에 그런 태그가 없다 (" << name << "). 있는 것: " << (names.empty() ? "없음" : names) << "\n";
    return std::nullopt;
}

std::optional<uint32_t> findRelation(Session& session, KiottySpecItemKind kind, std::string_view name)
{
    const KiottySpecModelResult model = KiottySpec_readModel(session.context, kind);
    std::string names;
    for (uint32_t index = 0; model.code == KIOTTY_SPEC_STATUS_SUCCESS && index < model.value.relation_count; ++index) {
        const KiottySpecRelationDefinitionResult definition = KiottySpec_readRelationDefinition(session.context, kind, index);
        if (definition.code != KIOTTY_SPEC_STATUS_SUCCESS) { continue; }
        if (definition.value.name == name) { return index; }
        names += (names.empty() ? "" : ", ") + std::string(definition.value.name);
    }
    std::cerr << "error: " << KiottySpec_kindName(kind) << " 에 그런 관계가 없다 (" << name << "). 있는 것: " << (names.empty() ? "없음" : names) << "\n";
    return std::nullopt;
}

std::optional<uint64_t> parseBits(const KiottySpecTagDefinition& definition, std::string_view names)
{
    if (names == "-") { return 0; }
    uint64_t value = 0;
    for (const std::string& name : split(names, ',')) {
        bool found = false;
        for (size_t bit = 0; bit < KIOTTY_SPEC_TAG_BIT_COUNT; ++bit) {
            if (definition.bit_names[bit][0] != '\0' && name == definition.bit_names[bit]) {
                value |= uint64_t{1} << bit;
                found = true;
            }
        }
        if (!found) {
            std::string known;
            for (size_t bit = 0; bit < KIOTTY_SPEC_TAG_BIT_COUNT; ++bit) {
                if (definition.bit_names[bit][0] != '\0') { known += (known.empty() ? "" : ", ") + std::string(definition.bit_names[bit]); }
            }
            std::cerr << "error: 태그 " << definition.name << " 에 그런 값이 없다 (" << name << "). 있는 것: " << known << "\n";
            return std::nullopt;
        }
    }
    return value;
}

std::string bitNames(const KiottySpecTagDefinition& definition, uint64_t value)
{
    std::string text;
    for (size_t bit = 0; bit < KIOTTY_SPEC_TAG_BIT_COUNT; ++bit) {
        if ((value & (uint64_t{1} << bit)) == 0) { continue; }
        if (!text.empty()) { text += '|'; }
        text += definition.bit_names[bit][0] != '\0' ? definition.bit_names[bit] : std::to_string(bit);
    }
    return text.empty() ? "-" : text;
}

}
