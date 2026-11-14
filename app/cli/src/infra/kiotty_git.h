#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace kiotty::cli {

enum class GitOutcome { FINISHED, MISSING, TIMED_OUT, BROKEN };

struct GitRun {
    GitOutcome outcome {GitOutcome::BROKEN};
    int exit_code {0};
    std::string output {};
};

GitRun runGit(const std::vector<std::string>& arguments, std::chrono::milliseconds limit);

}
