#pragma once

#include "arguments.h"

#include "kiotty_spec.h"

#include <functional>
#include <string>
#include <vector>

namespace kiotty::cli {

struct Session {
    KiottySpec *db = nullptr;   // needsDb 가 false 인 명령에서는 NULL
    std::string root;           // 프로젝트 루트 (UTF-8)
    KiottySpecActor actor = KiottySpecActor_Ai;
    const Arguments &args;      // 그룹·동사를 뺀 나머지
};

struct Command {
    std::string group;          // "item"
    std::string verb;           // "create" (그룹만으로 끝나는 명령은 빈 문자열)
    std::string usage;          // 인자 설명
    std::string summary;        // 한 줄 설명
    bool needsDb = true;
    std::function<void(Session &)> run;
};

const std::vector<Command> &commands();
std::string helpText();

} // namespace kiotty::cli
