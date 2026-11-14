#include "infra/kiotty_git.h"

#include "infra/kiotty_path_utf8.h"

#include <array>
#include <cstddef>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;
#endif

namespace kiotty::cli {

namespace {

using Clock = std::chrono::steady_clock;

constexpr size_t READ_CHUNK = 4096;

struct PipeRead {
    std::string text {};
    bool is_open {false};
};

#ifdef _WIN32
constexpr DWORD POLL_INTERVAL_MS = 10;

struct OwnedHandle {
    HANDLE value {nullptr};

    OwnedHandle() = default;
    explicit OwnedHandle(HANDLE handle) : value(handle) {}
    OwnedHandle(const OwnedHandle&) = delete;
    OwnedHandle& operator=(const OwnedHandle&) = delete;
    ~OwnedHandle() { reset(); }

    void reset()
    {
        const bool is_open = value != nullptr && value != INVALID_HANDLE_VALUE;
        if (is_open) { CloseHandle(value); }
        value = nullptr;
    }
};

struct OutputPipe {
    OwnedHandle reader {};
    OwnedHandle writer {};
    bool is_open {false};

    OutputPipe()
    {
        SECURITY_ATTRIBUTES attributes {};
        attributes.nLength = sizeof(attributes);
        attributes.bInheritHandle = TRUE;
        is_open = CreatePipe(&reader.value, &writer.value, &attributes, 0) != 0;
        SetHandleInformation(reader.value, HANDLE_FLAG_INHERIT, 0);
    }
};

struct KillingJob {
    OwnedHandle handle {};

    KillingJob() : handle(CreateJobObjectW(nullptr, nullptr))
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(handle.value, JobObjectExtendedLimitInformation, &limits,
                                sizeof(limits));
    }
};

std::wstring buildCommandLine(const std::vector<std::string>& arguments);
STARTUPINFOW describeStartup(HANDLE output);
GitRun describeStartFailure(DWORD error);
GitRun waitForGit(HANDLE process, HANDLE job, HANDLE reader, std::chrono::milliseconds limit);
std::wstring quoteArgument(const std::wstring& argument);
PipeRead readPipe(HANDLE reader);
#else
constexpr size_t PIPE_ENDS = 2;
constexpr size_t READ_END = 0;
constexpr size_t WRITE_END = 1;
constexpr std::chrono::milliseconds WAIT_INTERVAL {10};

std::vector<std::string> buildArgumentWords(const std::vector<std::string>& arguments);
GitRun describeSpawnFailure(int error);
GitRun waitForGit(pid_t child, int reader, std::chrono::milliseconds limit);
PipeRead readUntilClosed(int reader, Clock::time_point deadline);
GitRun waitForExit(pid_t child, Clock::time_point deadline);
int measureTimeLeft(Clock::time_point deadline);
#endif

}

#ifdef _WIN32
GitRun runGit(const std::vector<std::string>& arguments, std::chrono::milliseconds limit)
{
    OutputPipe pipe {};
    const KillingJob job {};
    const bool is_ready = pipe.is_open && job.handle.value != nullptr;
    if (!is_ready) { return GitRun{}; }
    std::wstring command_line = buildCommandLine(arguments);
    STARTUPINFOW startup = describeStartup(pipe.writer.value);
    PROCESS_INFORMATION process {};
    const BOOL started =
        CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED,
                       nullptr, nullptr, &startup, &process);
    if (started == 0) { return describeStartFailure(GetLastError()); }
    const OwnedHandle process_handle(process.hProcess);
    const OwnedHandle thread_handle(process.hThread);
    AssignProcessToJobObject(job.handle.value, process.hProcess);
    ResumeThread(process.hThread);
    pipe.writer.reset();
    return waitForGit(process.hProcess, job.handle.value, pipe.reader.value, limit);
}
#else
GitRun runGit(const std::vector<std::string>& arguments, std::chrono::milliseconds limit)
{
    std::array<int, PIPE_ENDS> ends {-1, -1};
    const int piped = pipe(ends.data());
    if (piped != 0) { return GitRun{}; }
    posix_spawn_file_actions_t actions {};
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, ends.at(WRITE_END), STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, ends.at(READ_END));
    posix_spawn_file_actions_addclose(&actions, ends.at(WRITE_END));
    std::vector<std::string> words = buildArgumentWords(arguments);
    std::vector<char*> argv {};
    const size_t count = words.size();
    for (size_t index = 0; index < count; ++index) { argv.push_back(words.at(index).data()); }
    argv.push_back(nullptr);
    pid_t child = 0;
    const int spawned = posix_spawnp(&child, "git", &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(ends.at(WRITE_END));
    if (spawned != 0) {
        close(ends.at(READ_END));
        return describeSpawnFailure(spawned);
    }
    const GitRun run = waitForGit(child, ends.at(READ_END), limit);
    close(ends.at(READ_END));
    return run;
}
#endif

namespace {

#ifdef _WIN32
std::wstring buildCommandLine(const std::vector<std::string>& arguments)
{
    std::wstring line = L"git";
    for (const std::string& argument : arguments) {
        line += L' ';
        line += quoteArgument(pathFromUtf8(argument).wstring());
    }
    return line;
}

STARTUPINFOW describeStartup(HANDLE output)
{
    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = output;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    return startup;
}

GitRun describeStartFailure(DWORD error)
{
    const bool is_missing = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    GitRun run {};
    run.outcome = is_missing ? GitOutcome::MISSING : GitOutcome::BROKEN;
    return run;
}

GitRun waitForGit(HANDLE process, HANDLE job, HANDLE reader, std::chrono::milliseconds limit)
{
    GitRun run {};
    const Clock::time_point deadline = Clock::now() + limit;
    bool is_exited = false;
    while (!is_exited) {
        run.output += readPipe(reader).text;
        is_exited = WaitForSingleObject(process, POLL_INTERVAL_MS) == WAIT_OBJECT_0;
        const bool is_late = !is_exited && Clock::now() >= deadline;
        if (is_late) {
            TerminateJobObject(job, 1);
            run.outcome = GitOutcome::TIMED_OUT;
            return run;
        }
    }
    run.output += readPipe(reader).text;
    DWORD code = 0;
    GetExitCodeProcess(process, &code);
    run.outcome = GitOutcome::FINISHED;
    run.exit_code = static_cast<int>(code);
    return run;
}

std::wstring quoteArgument(const std::wstring& argument)
{
    const bool is_plain =
        !argument.empty() && argument.find_first_of(L" \t\"") == std::wstring::npos;
    if (is_plain) { return argument; }
    std::wstring quoted = L"\"";
    size_t slashes = 0;
    for (const wchar_t letter : argument) {
        const bool is_slash = letter == L'\\';
        slashes = is_slash ? slashes + 1 : slashes;
        if (is_slash) { continue; }
        const bool is_quote = letter == L'"';
        quoted.append(slashes, L'\\');
        quoted.append(is_quote ? slashes + 1 : 0, L'\\');
        quoted += letter;
        slashes = 0;
    }
    quoted.append(slashes, L'\\');
    quoted.append(slashes, L'\\');
    return quoted + L"\"";
}

PipeRead readPipe(HANDLE reader)
{
    PipeRead result {};
    std::array<char, READ_CHUNK> buffer {};
    while (true) {
        DWORD available = 0;
        const BOOL peeked = PeekNamedPipe(reader, nullptr, 0, nullptr, &available, nullptr);
        result.is_open = peeked != 0;
        const bool is_empty = peeked == 0 || available == 0;
        if (is_empty) { return result; }
        const DWORD wanted = available < READ_CHUNK ? available : static_cast<DWORD>(READ_CHUNK);
        DWORD received = 0;
        const BOOL filled = ReadFile(reader, buffer.data(), wanted, &received, nullptr);
        const bool is_broken = filled == 0 || received == 0;
        if (is_broken) { return PipeRead{result.text, false}; }
        result.text.append(buffer.data(), received);
    }
}
#else
std::vector<std::string> buildArgumentWords(const std::vector<std::string>& arguments)
{
    std::vector<std::string> words = {"git"};
    words.insert(words.end(), arguments.begin(), arguments.end());
    return words;
}

GitRun describeSpawnFailure(int error)
{
    GitRun run {};
    run.outcome = error == ENOENT ? GitOutcome::MISSING : GitOutcome::BROKEN;
    return run;
}

GitRun waitForGit(pid_t child, int reader, std::chrono::milliseconds limit)
{
    const Clock::time_point deadline = Clock::now() + limit;
    const PipeRead read = readUntilClosed(reader, deadline);
    GitRun run = waitForExit(child, deadline);
    run.output = read.text;
    return run;
}

PipeRead readUntilClosed(int reader, Clock::time_point deadline)
{
    PipeRead result {};
    std::array<char, READ_CHUNK> buffer {};
    while (true) {
        const int left = measureTimeLeft(deadline);
        if (left <= 0) { return result; }
        pollfd watched {reader, POLLIN, 0};
        const int ready = poll(&watched, 1, left);
        if (ready <= 0) { continue; }
        const ssize_t received = read(reader, buffer.data(), buffer.size());
        if (received <= 0) { return result; }
        result.text.append(buffer.data(), static_cast<size_t>(received));
    }
}

GitRun waitForExit(pid_t child, Clock::time_point deadline)
{
    GitRun run {};
    int status = 0;
    while (true) {
        const pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child) { break; }
        if (waited < 0) { return run; }
        const bool is_late = Clock::now() >= deadline;
        if (is_late) {
            kill(child, SIGKILL);
            waitpid(child, nullptr, 0);
            run.outcome = GitOutcome::TIMED_OUT;
            return run;
        }
        std::this_thread::sleep_for(WAIT_INTERVAL);
    }
    const bool is_exited = WIFEXITED(status);
    if (!is_exited) { return run; }
    run.outcome = GitOutcome::FINISHED;
    run.exit_code = WEXITSTATUS(status);
    return run;
}

int measureTimeLeft(Clock::time_point deadline)
{
    const std::chrono::milliseconds left =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
    return static_cast<int>(left.count());
}
#endif

}

}
