#include "kiotty_path_utf8.h"
#include "kiotty_session.h"

#include "kiotty_spec.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace {

using namespace kiotty::cli;

constexpr size_t GROUP_AND_VERB_TOKENS = 2;

struct CommandMatch {
    size_t index {};
    size_t consumed {};
};

int runTokens(const std::vector<std::string>& tokens);
int runCommand(const CommandEntry& command, const Arguments& arguments);
void printHelp();
void printCommandHelp(const CommandEntry& command);
std::optional<CommandMatch> findCommand(const std::vector<std::string>& tokens);
KiottySpecActor findActor(const Arguments& arguments);
std::string findRoot(const Arguments& arguments);
std::string makeDatabasePath(const std::string& root);
std::string readEnvironment(const char* name);

}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    std::vector<std::string> tokens {};
    for (int index = 1; index < argc; ++index) {
        const int size =
            WideCharToMultiByte(CP_UTF8, 0, argv[index], -1, nullptr, 0, nullptr, nullptr);
        std::string token(static_cast<size_t>(size > 0 ? size - 1 : 0), '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv[index], -1, token.data(), size, nullptr, nullptr);
        tokens.push_back(std::move(token));
    }
    return runTokens(tokens);
}
#else
int main(int argc, char** argv)
{
    return runTokens(std::vector<std::string>(argv + 1, argv + argc));
}
#endif

namespace {

int runTokens(const std::vector<std::string>& tokens)
{
    const std::string first_token = tokens.empty() ? std::string() : tokens.front();
    const bool is_help = tokens.empty() || first_token == "help" || first_token == "--help" ||
                         first_token == "-h";
    if (is_help) {
        printHelp();
        return 0;
    }
    const std::optional<CommandMatch> match = findCommand(tokens);
    if (!match) {
        const bool has_verb_token = tokens.size() > 1;
        const std::string verb_token = has_verb_token ? " " + tokens.at(1) : std::string();
        return usage(formatMessage(MessageKey::UNKNOWN_COMMAND, {first_token + verb_token}));
    }
    const CommandMatch found = *match;
    const CommandEntry& command = commands().at(found.index);
    const bool is_diff = command.group == "diff";
    const std::vector<std::string_view> extra_flags =
        is_diff ? DIFF_FLAGS : std::vector<std::string_view>();
    const Arguments arguments = parseArguments(tokens, found.consumed, extra_flags);
    const bool has_help_flag = arguments.flag("help");
    if (has_help_flag) {
        printCommandHelp(command);
        return 0;
    }
    return runCommand(command, arguments);
}

int runCommand(const CommandEntry& command, const Arguments& arguments)
{
    Session session {};
    session.arguments = arguments;
    session.actor = findActor(arguments);
    const std::string database = makeDatabasePath(findRoot(arguments));
    KiottySpecConfiguration configuration {};
    const KiottySpecContextResult created = KiottySpec_createContext(&configuration);
    session.context = created.value;
    const KiottySpecStatus opened = created.code == KIOTTY_SPEC_STATUS_SUCCESS
                                        ? KiottySpec_open(session.context, textOf(database)).code
                                        : created.code;
    const int code =
        opened != KIOTTY_SPEC_STATUS_SUCCESS ? fail(session, opened) : command.run(session);
    KiottySpec_releaseContext(session.context);
    return code;
}

void printHelp()
{
    std::cout << findMessage(MessageKey::HELP_TEXT);
    for (const CommandEntry& command : commands()) {
        std::cout << "  ks " << command.group << (command.verb.empty() ? "" : " ")
                  << command.verb << " " << findMessage(command.usage) << "\n      "
                  << findMessage(command.summary) << "\n";
    }
}

void printCommandHelp(const CommandEntry& command)
{
    std::cout << "ks " << command.group << (command.verb.empty() ? "" : " ") << command.verb
              << " " << findMessage(command.usage) << "\n  " << findMessage(command.summary)
              << "\n";
}

std::optional<CommandMatch> findCommand(const std::vector<std::string>& tokens)
{
    const std::string_view group = tokens.front();
    const bool has_verb_token = tokens.size() > 1;
    const std::string_view verb = has_verb_token ? std::string_view(tokens.at(1)) : "";
    const std::vector<CommandEntry>& entries = commands();
    const size_t count = entries.size();
    for (size_t index = 0; index < count; ++index) {
        const CommandEntry& command = entries[index];
        if (group != command.group) { continue; }
        const bool is_group_only = command.verb.empty();
        if (is_group_only) { return CommandMatch{index, 1}; }
        const bool is_verb_match = has_verb_token && verb == command.verb;
        if (is_verb_match) { return CommandMatch{index, GROUP_AND_VERB_TOKENS}; }
    }
    return std::nullopt;
}

KiottySpecActor findActor(const Arguments& arguments)
{
    const std::string name =
        arguments.option("actor").value_or(readEnvironment("KIOTTY_SPEC_ACTOR"));
    return name == "user" ? KIOTTY_SPEC_ACTOR_USER : KIOTTY_SPEC_ACTOR_AI;
}

std::string findRoot(const Arguments& arguments)
{
    const std::optional<std::string> root = arguments.option("root");
    if (root) { return *root; }
    const std::string environment_root = readEnvironment("KIOTTY_SPEC_ROOT");
    const bool has_environment_root = !environment_root.empty();
    if (has_environment_root) { return environment_root; }
    std::error_code error {};
    const std::filesystem::path current = std::filesystem::current_path(error);
    std::filesystem::path probe = current;
    while (true) {
        const bool is_empty = probe.empty();
        if (is_empty) { break; }
        const bool is_directory = std::filesystem::is_directory(probe / ".kiotty", error);
        if (is_directory) { return pathToUtf8(probe); }
        const std::filesystem::path parent = probe.parent_path();
        if (probe == parent) { break; }
        probe = parent;
    }
    return pathToUtf8(current);
}

std::string makeDatabasePath(const std::string& root)
{ return pathToUtf8(std::filesystem::u8path(root) / ".kiotty" / "spec.kisp"); }

std::string readEnvironment(const char* name)
{
#ifdef _WIN32
    const std::wstring wide_name = pathFromUtf8(name).wstring();
    const DWORD capacity = GetEnvironmentVariableW(wide_name.c_str(), nullptr, 0);
    if (capacity == 0) { return ""; }
    std::wstring value(capacity, L'\0');
    const DWORD length = GetEnvironmentVariableW(wide_name.c_str(), value.data(), capacity);
    const bool is_complete = length > 0 && length < capacity;
    if (!is_complete) { return ""; }
    value.resize(length);
    return utf8FromWide(value);
#else
    const char* value = std::getenv(name);
    return value != nullptr ? value : "";
#endif
}

}
