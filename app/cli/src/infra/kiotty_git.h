#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace kiotty::cli {

enum class GitOutcome { FINISHED, MISSING, TIMED_OUT, BROKEN };
enum class ErrorStream { INHERITED, CAPTURED };

struct GitRun {
    GitOutcome outcome {GitOutcome::BROKEN};
    int exit_code {0};
    std::string output {};
};

GitRun runGit(const std::vector<std::string>& arguments, std::chrono::milliseconds limit);
GitRun runProgram(std::string_view program, const std::vector<std::string>& arguments,
                  std::chrono::milliseconds limit, ErrorStream error_stream);

}
