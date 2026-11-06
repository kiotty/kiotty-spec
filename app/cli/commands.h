#pragma once

#include "arguments.h"

#include "kiotty_spec.h"

#include <functional>
#include <string>
#include <vector>

namespace kiotty::cli {

struct Session {
    KiottySpec *db = nullptr;
    std::string root;
    KiottySpecActor actor = KIOTTY_SPEC_ACTOR_AI;
    const Arguments &args;
};

struct Command {
    std::string group;
    std::string verb;
    std::string usage;
    std::string summary;
    bool needsDb = true;
    std::function<void(Session &)> run;
};

const std::vector<Command> &commands();
std::string helpText();

}
