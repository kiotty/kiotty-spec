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

std::string environment(const char* name)
{
#ifdef _WIN32
    char* value = nullptr;
    size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) { return ""; }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? value : "";
#endif
}

std::string findRoot(const Arguments& args)
{
    if (const auto root = args.option("root")) { return *root; }
    if (const auto env = environment("KIOTTY_SPEC_ROOT"); !env.empty()) { return env; }
    std::error_code error;
    const auto current = std::filesystem::current_path(error);
    for (auto probe = current; !probe.empty(); probe = probe.parent_path()) {
        if (std::filesystem::is_directory(probe / ".kiotty", error)) { return pathToUtf8(probe); }
        if (probe == probe.parent_path()) { break; }
    }
    return pathToUtf8(current);
}

KiottySpecActor findActor(const Arguments& args)
{
    const std::string name = args.option("actor").value_or(environment("KIOTTY_SPEC_ACTOR"));
    return name == "user" ? KIOTTY_SPEC_ACTOR_USER : KIOTTY_SPEC_ACTOR_AI;
}

void printHelp()
{
    std::cout << "사용법: ks <그룹> [동사] [인자...] [--root DIR] [--actor ai|user]\n"
                 "출력은 TSV 이며 첫 줄이 머리글이다. 쓰기 명령은 성공하면 아무것도 찍지 않는다 (만든 ID 는 찍는다).\n"
                 "Kind: change_request requirement rule external_interface design unit_test_plan milestone unit_test_result\n"
                 "      system_test_plan system_test_result code_inspection_result e2e_test_plan e2e_test_result\n\n";
    for (const CommandEntry& command : commands()) {
        std::cout << "  ks " << command.group << (command.verb[0] != '\0' ? " " : "") << command.verb << " " << command.usage << "\n      "
                  << command.summary << "\n";
    }
}

const CommandEntry* findCommand(const std::vector<std::string>& tokens, size_t& consumed)
{
    for (const CommandEntry& command : commands()) {
        if (tokens[0] != command.group) { continue; }
        if (command.verb[0] == '\0') {
            consumed = 1;
            return &command;
        }
        if (tokens.size() > 1 && tokens[1] == command.verb) {
            consumed = 2;
            return &command;
        }
    }
    return nullptr;
}

int run(const std::vector<std::string>& tokens)
{
    if (tokens.empty() || tokens[0] == "help" || tokens[0] == "--help" || tokens[0] == "-h") {
        printHelp();
        return 0;
    }
    size_t consumed = 0;
    const CommandEntry* command = findCommand(tokens, consumed);
    if (command == nullptr) { return usage("알 수 없는 명령 '" + tokens[0] + (tokens.size() > 1 ? " " + tokens[1] : "") + "'. `ks help` 참고"); }
    Session session;
    const bool diff = std::string_view(command->group) == "diff";
    session.args = parseArguments(tokens, consumed, diff ? DIFF_FLAGS : std::vector<std::string_view>());
    if (session.args.flag("help")) {
        std::cout << "ks " << command->group << (command->verb[0] != '\0' ? " " : "") << command->verb << " " << command->usage << "\n  "
                  << command->summary << "\n";
        return 0;
    }
    session.actor = findActor(session.args);
    const std::string root = findRoot(session.args);
    KiottySpecConfiguration configuration {};
    configuration.project_root = textOf(root);
    const KiottySpecContextResult created = KiottySpec_createContext(&configuration);
    session.context = created.value;
    int code = 0;
    if (created.code != KIOTTY_SPEC_STATUS_SUCCESS) {
        code = fail(session, created.code);
    } else {
        code = command->run(session);
    }
    KiottySpec_releaseContext(session.context);
    return code;
}

}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    std::vector<std::string> tokens;
    for (int index = 1; index < argc; ++index) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, argv[index], -1, nullptr, 0, nullptr, nullptr);
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
