#include "kiotty_viewer_bridge.h"

#include "kiotty_viewer_names.h"
#include "kiotty_viewer_query.h"

#include <algorithm>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

namespace {

using kiotty::viewer::isSuccess;
using kiotty::viewer::ItemListStyle;
using kiotty::viewer::JsonScope;
using kiotty::viewer::JsonWriter;
using kiotty::viewer::makeText;
using kiotty::viewer::Project;
using kiotty::viewer::QueryWriter;

constexpr std::string_view META_TABLE = "TABLE\tmeta\t";
constexpr std::string_view TABLE_PREFIX = "TABLE\t";
constexpr std::string_view NAME_LINE = "meta\tO\tproject.name\t";
constexpr std::string_view CREATED_LINE = "meta\tO\tproject.created_at\t";
constexpr std::string_view ITEMS_REQUEST = "items:";
constexpr std::string_view ITEM_REQUEST = "item:";
constexpr std::string_view DIGITS = "0123456789";
constexpr size_t MAX_NUMBER_DIGITS = 18;
constexpr uint64_t EIGHT_FOLD_SHIFT = 3;

struct ContextRelease {
    void operator()(KiottySpecContext* context) const { KiottySpec_releaseContext(context); }
};

using ContextOwner = std::unique_ptr<KiottySpecContext, ContextRelease>;

struct Bridge {
    ContextOwner context {};
    Project project {};
    std::string response {};
};

Bridge g_bridge {};

void writeOpened();
void writeFailure(std::string_view status, KiottySpecSubject subject, std::string_view text);
void writeLastError(KiottySpecContext* context, KiottySpecStatus fallback);
void runQuery(std::string_view request);
void runItems(std::string_view kind_name, ItemListStyle style);
void runItem(std::string_view id);
std::string formatError(std::string_view status, KiottySpecSubject subject,
                        const KiottySpecError& error);
Project readProject(const std::string& path);
Project readMetaLine(Project project, std::string_view line);
bool hasPrefix(std::string_view text, std::string_view prefix);

}

extern "C" {

const char* KiottyViewer_open(const char* path)
{
    const std::string file = path ? std::string(path) : std::string();
    const bool is_missing = !std::ifstream(file, std::ios::binary);
    if (is_missing) {
        writeFailure("NOT_FOUND", KIOTTY_SPEC_SUBJECT_FILE, file);
        return g_bridge.response.c_str();
    }
    const KiottySpecConfiguration configuration {};
    const KiottySpecContextResult created = KiottySpec_createContext(&configuration);
    ContextOwner context(created.value);
    const bool is_created = isSuccess(created.code);
    if (!is_created) {
        const std::string status = KiottySpec_formatStatus(created.code).value.value;
        writeFailure(status, KIOTTY_SPEC_SUBJECT_CONFIGURATION, "");
        return g_bridge.response.c_str();
    }
    const KiottySpecStatusResult opened = KiottySpec_open(context.get(), makeText(file));
    const bool is_opened = isSuccess(opened.code);
    if (!is_opened) {
        writeLastError(context.get(), opened.code);
        return g_bridge.response.c_str();
    }
    g_bridge.context = std::move(context);
    g_bridge.project = readProject(file);
    writeOpened();
    return g_bridge.response.c_str();
}

const char* KiottyViewer_query(const char* request)
{
    const bool is_open = static_cast<bool>(g_bridge.context);
    const std::string text = request ? std::string(request) : std::string();
    if (is_open) { runQuery(text); }
    else { writeFailure("NOT_OPEN", KIOTTY_SPEC_SUBJECT_FILE, ""); }
    return g_bridge.response.c_str();
}

void KiottyViewer_close()
{
    g_bridge.context.reset();
    g_bridge.project = Project {};
}

}

namespace {

void writeOpened()
{
    g_bridge.response.clear();
    JsonWriter json(g_bridge.response);
    const bool has_name = !g_bridge.project.name.empty();
    const uint64_t created_at = static_cast<uint64_t>(g_bridge.project.created_at);
    json.open(JsonScope::OBJECT);
    json.writeKey("ok");
    json.writeLiteral("true");
    json.writeKey("project");
    json.open(JsonScope::OBJECT);
    json.writeKey("name");
    if (has_name) { json.writeString(g_bridge.project.name); }
    else { json.writeLiteral("null"); }
    json.writeKey("created_at");
    if (g_bridge.project.has_created_at) { json.writeNumber(created_at); }
    else { json.writeLiteral("null"); }
    json.close(JsonScope::OBJECT);
    json.close(JsonScope::OBJECT);
}

void runQuery(std::string_view request)
{
    const bool is_items = hasPrefix(request, ITEMS_REQUEST);
    const bool is_item = hasPrefix(request, ITEM_REQUEST);
    g_bridge.response.clear();
    QueryWriter writer(*g_bridge.context, g_bridge.project, g_bridge.response);
    if (request == "overview") { writer.writeOverview(); }
    else if (request == "progress") { writer.writeProgress(); }
    else if (request == "changes") { runItems("change_request", ItemListStyle::WITH_OUT_COUNT); }
    else if (is_items) { runItems(request.substr(ITEMS_REQUEST.size()), ItemListStyle::PLAIN); }
    else if (is_item) { runItem(request.substr(ITEM_REQUEST.size())); }
    else { writeFailure("INVALID_ARGUMENT", KIOTTY_SPEC_SUBJECT_NONE, request); }
}

void runItems(std::string_view kind_name, ItemListStyle style)
{
    const KiottySpecItemKind kind = KiottySpec_parseKind(makeText(kind_name));
    const bool is_unknown = static_cast<int>(kind) == 0;
    if (is_unknown) {
        writeFailure("NOT_FOUND", KIOTTY_SPEC_SUBJECT_KIND, kind_name);
        return;
    }
    g_bridge.response.clear();
    QueryWriter(*g_bridge.context, g_bridge.project, g_bridge.response).writeItems(kind, style);
}

void runItem(std::string_view id)
{
    const KiottySpecItemResult item = KiottySpec_readItem(g_bridge.context.get(), makeText(id));
    const bool has_item = isSuccess(item.code);
    if (!has_item) {
        writeLastError(g_bridge.context.get(), item.code);
        return;
    }
    g_bridge.response.clear();
    QueryWriter(*g_bridge.context, g_bridge.project, g_bridge.response).writeItem(item);
}

void writeFailure(std::string_view status, KiottySpecSubject subject, std::string_view text)
{
    KiottySpecError error {};
    const size_t length = std::min<size_t>(text.size(), KIOTTY_SPEC_MAX_ERROR_TEXT_LENGTH);
    text.copy(error.text, length);
    g_bridge.response = formatError(status, subject, error);
}

void writeLastError(KiottySpecContext* context, KiottySpecStatus fallback)
{
    const KiottySpecErrorResult last = KiottySpec_readLastError(context);
    const bool has_error = isSuccess(last.code) && !isSuccess(last.value.status);
    const KiottySpecStatus status = has_error ? last.value.status : fallback;
    const KiottySpecError error = has_error ? last.value : KiottySpecError {};
    const KiottySpecSubject subject = has_error ? last.value.subject : KIOTTY_SPEC_SUBJECT_NONE;
    g_bridge.response = formatError(KiottySpec_formatStatus(status).value.value, subject, error);
}

std::string formatError(std::string_view status, KiottySpecSubject subject,
                        const KiottySpecError& error)
{
    std::string out {};
    JsonWriter json(out);
    json.open(JsonScope::OBJECT);
    json.writeKey("ok");
    json.writeLiteral("false");
    json.writeKey("error");
    json.open(JsonScope::OBJECT);
    json.writeKey("status");
    json.writeString(status);
    json.writeKey("subject");
    json.writeString(kiotty::viewer::formatSubject(subject));
    json.writeKey("line");
    json.writeNumber(error.line);
    json.writeKey("text");
    json.writeString(error.text);
    json.close(JsonScope::OBJECT);
    json.close(JsonScope::OBJECT);
    return out;
}

Project readProject(const std::string& path)
{
    Project project {};
    std::ifstream file(path, std::ios::binary);
    std::string line {};
    bool is_meta = false;
    while (true) {
        const bool has_line = static_cast<bool>(std::getline(file, line));
        if (!has_line) { break; }
        const bool has_return = !line.empty() && line.back() == '\r';
        if (has_return) { line.pop_back(); }
        const bool is_table = hasPrefix(line, TABLE_PREFIX);
        const bool is_meta_end = is_table && is_meta;
        if (is_meta_end) { break; }
        if (is_table) { is_meta = hasPrefix(line, META_TABLE); }
        else if (is_meta) { project = readMetaLine(project, line); }
    }
    return project;
}

Project readMetaLine(Project project, std::string_view line)
{
    const bool is_name = hasPrefix(line, NAME_LINE);
    const bool is_created = hasPrefix(line, CREATED_LINE);
    if (is_name) { project.name = std::string(line.substr(NAME_LINE.size())); }
    if (!is_created) { return project; }
    const std::string_view digits = line.substr(CREATED_LINE.size());
    const size_t first_other = digits.find_first_not_of(DIGITS);
    const bool is_digits = !digits.empty() && first_other == std::string_view::npos;
    const bool is_short = digits.size() <= MAX_NUMBER_DIGITS;
    const bool is_number = is_digits && is_short;
    uint64_t value = 0;
    for (const char letter : digits) {
        value = (value << EIGHT_FOLD_SHIFT) + (value << 1) + static_cast<uint64_t>(letter - '0');
    }
    project.created_at = is_number ? static_cast<int64_t>(value) : 0;
    project.has_created_at = is_number;
    return project;
}

bool hasPrefix(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

}
