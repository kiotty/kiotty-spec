#pragma once

#include "kiotty_spec.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kiotty::cli {

// 명령 줄 인자. "--이름 값" 은 옵션(여러 번 줄 수 있음), 값을 받지 않는 이름은 플래그다.
struct Arguments {
    std::vector<std::string> positional;
    std::vector<std::pair<std::string, std::string>> options;
    std::vector<std::string> flags;

    std::optional<std::string> option(std::string_view name) const;
    std::vector<std::string> all(std::string_view name) const;
    bool flag(std::string_view name) const;
};

// extra_flags 는 그 명령에서만 값을 받지 않는 이름이다 (ks diff 의 --title 같은 것).
Arguments parseArguments(const std::vector<std::string>& tokens, size_t first, const std::vector<std::string_view>& extra_flags);

struct Session {
    KiottySpecContext* context {nullptr};
    KiottySpecActor actor {KIOTTY_SPEC_ACTOR_AI};
    Arguments args;
};

// 실패를 알리고 종료 코드를 돌려준다. 라이브러리 실패는 1, 사용법 실패는 2.
int fail(Session& session, KiottySpecStatus status);
int usage(const std::string& message);

KiottySpecText textOf(std::string_view text);
std::string kindName(KiottySpecItemKind kind);
std::string stateName(KiottySpecCheckpointState state);
std::string cell(std::string_view value);
void printRow(const std::vector<std::string>& cells);

std::string checkpointName(KiottySpecCheckpointHandle handle);
std::optional<KiottySpecCheckpointHandle> parseCheckpoint(std::string_view text);
std::optional<std::string> readFileText(const std::string& path);
// 영문 이름 (design) 이나 한글 모델 이름 (설계 사항). 모르면 받는 값을 stderr 에 찍고 nullopt
std::optional<KiottySpecItemKind> kindArgument(Session& session, std::string_view text);
std::string itemId(Session& session, KiottySpecItemHandle handle);

// 모델 메타데이터에서 태그 · 관계 이름을 index 로 바꾼다 (설계서 v1 8.4).
struct TagColumn {
    uint32_t index;
    KiottySpecTagDefinition definition;
};

std::optional<TagColumn> findTag(Session& session, KiottySpecItemKind kind, std::string_view name);
std::optional<uint32_t> findRelation(Session& session, KiottySpecItemKind kind, std::string_view name);
std::optional<uint64_t> parseBits(const KiottySpecTagDefinition& definition, std::string_view names);
std::string bitNames(const KiottySpecTagDefinition& definition, uint64_t value);

using Command = int (*)(Session&);

struct CommandEntry {
    const char* group;
    const char* verb;
    const char* usage;
    const char* summary;
    Command run;
};

const std::vector<CommandEntry>& commands();

int runDiff(Session& session);
extern const std::vector<std::string_view> DIFF_FLAGS;

}
