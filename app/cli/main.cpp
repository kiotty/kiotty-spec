// ks — AI 와 사용자가 Kiotty-Spec 로컬 DB 에 질의하는 CLI. include/kiotty_spec.h 만 사용한다.

#include "arguments.h"
#include "commands.h"
#include "output.h"

#include "kiotty_spec.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

using namespace kiotty::cli;

std::string toUtf8(const std::filesystem::path &path) {
    const std::u8string u8 = path.generic_u8string();
    return std::string(reinterpret_cast<const char *>(u8.data()), u8.size());
}

std::string environment(const char *name) {
#ifdef _WIN32
    char *value = nullptr;
    size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) return "";
    std::string result(value);
    std::free(value);
    return result;
#else
    const char *value = std::getenv(name);
    return value != nullptr ? value : "";
#endif
}

// --root, KIOTTY_SPEC_ROOT, 위로 올라가며 .kiotty 를 찾기, 현재 위치 순.
std::string findRoot(const Arguments &args) {
    if (const auto root = args.option("root")) return *root;
    if (const auto env = environment("KIOTTY_SPEC_ROOT"); !env.empty()) return env;
    std::error_code ec;
    auto dir = std::filesystem::current_path(ec);
    for (auto probe = dir; !probe.empty(); probe = probe.parent_path()) {
        if (std::filesystem::is_directory(probe / ".kiotty", ec)) return toUtf8(probe);
        if (probe == probe.parent_path()) break;
    }
    return toUtf8(dir);
}

KiottySpecActor findActor(const Arguments &args) {
    std::string name = args.option("actor").value_or(environment("KIOTTY_SPEC_ACTOR"));
    if (name.empty()) name = "ai";
    return static_cast<KiottySpecActor>(enumValue("Actor", name));
}

const Command *findCommand(const std::vector<std::string> &tokens, size_t &consumed) {
    if (tokens.empty()) return nullptr;
    for (const auto &command : commands()) {
        if (command.group != tokens[0]) continue;
        if (command.verb.empty()) {
            consumed = 1;
            return &command;
        }
        if (tokens.size() > 1 && command.verb == tokens[1]) {
            consumed = 2;
            return &command;
        }
    }
    return nullptr;
}

int run(std::vector<std::string> tokens) {
    if (tokens.empty() || tokens[0] == "help" || tokens[0] == "--help" || tokens[0] == "-h") {
        std::cout << helpText();
        return 0;
    }
    size_t consumed = 0;
    const Command *command = findCommand(tokens, consumed);
    if (command == nullptr) {
        std::cerr << "error: Usage: 알 수 없는 명령 '" << tokens[0] << (tokens.size() > 1 ? " " + tokens[1] : "")
                  << "'. `ks help` 참고\n";
        return 2;
    }
    const Arguments args(std::vector<std::string>(tokens.begin() + static_cast<std::ptrdiff_t>(consumed), tokens.end()));
    if (args.flag("help")) {
        std::cout << "ks " << command->group << (command->verb.empty() ? "" : " " + command->verb) << " " << command->usage
                  << "\n  " << command->summary << "\n";
        return 0;
    }

    Session session{nullptr, findRoot(args), findActor(args), args};
    if (command->needsDb) {
        check(KiottySpec_open(session.root.c_str(), &session.db));
    }
    struct Closer {
        KiottySpec *db;
        ~Closer() { if (db != nullptr) KiottySpec_close(db); }
    } closer{session.db};
    command->run(session);
    return 0;
}

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    std::vector<std::string> tokens;
    for (int i = 1; i < argc; ++i) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string token(static_cast<size_t>(size > 0 ? size - 1 : 0), '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, token.data(), size, nullptr, nullptr);
        tokens.push_back(std::move(token));
    }
#else
int main(int argc, char **argv) {
    std::vector<std::string> tokens(argv + 1, argv + argc);
#endif
    try {
        return run(std::move(tokens));
    } catch (const UsageError &e) {
        std::cerr << "error: Usage: " << e.what() << "\n";
        return 2;
    } catch (const CallError &e) {
        std::cerr << "error: " << KiottySpec_strerror(e.status) << ": " << e.what() << "\n";
        return 1;
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
