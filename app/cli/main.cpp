#include "kiotty_path_utf8.h"
#include "kiotty_session.h"

#include "kiotty_spec.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace {

using namespace kiotty::cli;

constexpr size_t GROUP_AND_VERB_TOKENS = 2;

int run(const std::vector<std::string>& tokens);
void printHelp();
const CommandEntry* findCommand(const std::vector<std::string>& tokens, size_t& consumed);
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
    return run(tokens);
}
#else
int main(int argc, char** argv)
{
    return run(std::vector<std::string>(argv + 1, argv + argc));
}
#endif

namespace {

int run(const std::vector<std::string>& tokens)
{
    const std::string first_token = tokens.empty() ? std::string() : tokens.front();
    const bool is_help = tokens.empty() || first_token == "help" || first_token == "--help" ||
                         first_token == "-h";
    if (is_help) {
        printHelp();
        return 0;
    }
    size_t consumed = 0;
    const CommandEntry* command = findCommand(tokens, consumed);
    if (command == nullptr) {
        const bool has_verb_token = tokens.size() > 1;
        const std::string verb_token = has_verb_token ? " " + tokens.at(1) : std::string();
        return usage(formatMessage(MessageKey::UNKNOWN_COMMAND, {first_token + verb_token}));
    }
    Session session {};
    const bool is_diff = command->group == "diff";
    session.args =
        parseArguments(tokens, consumed, is_diff ? DIFF_FLAGS : std::vector<std::string_view>());
    const bool has_help_flag = session.args.flag("help");
    if (has_help_flag) {
        std::cout << "ks " << command->group << (command->verb.empty() ? "" : " ")
                  << command->verb << " " << findMessage(command->usage) << "\n  "
                  << findMessage(command->summary) << "\n";
        return 0;
    }
    session.actor = findActor(session.args);
    const std::string database = makeDatabasePath(findRoot(session.args));
    KiottySpecConfiguration configuration {};
    const KiottySpecContextResult created = KiottySpec_createContext(&configuration);
    session.context = created.value;
    const KiottySpecStatus opened = created.code == KIOTTY_SPEC_STATUS_SUCCESS
                                        ? KiottySpec_open(session.context, textOf(database)).code
                                        : created.code;
    const int code =
        opened != KIOTTY_SPEC_STATUS_SUCCESS ? fail(session, opened) : command->run(session);
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

const CommandEntry* findCommand(const std::vector<std::string>& tokens, size_t& consumed)
{
    const std::string_view group = tokens.front();
    const bool has_verb_token = tokens.size() > 1;
    const std::string_view verb = has_verb_token ? std::string_view(tokens.at(1)) : "";
    for (const CommandEntry& command : commands()) {
        if (group != command.group) { continue; }
        const bool is_group_only = command.verb.empty();
        if (is_group_only) {
            consumed = 1;
            return &command;
        }
        if (has_verb_token && verb == command.verb) {
            consumed = GROUP_AND_VERB_TOKENS;
            return &command;
        }
    }
    return nullptr;
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
    const auto current = std::filesystem::current_path(error);
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
    wchar_t* value = nullptr;
    size_t size = 0;
    const errno_t duplicated = _wdupenv_s(&value, &size, wide_name.c_str());
    if (duplicated != 0 || value == nullptr) { return ""; }
    const std::string result = utf8FromWide(std::wstring_view(value));
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? value : "";
#endif
}

}
