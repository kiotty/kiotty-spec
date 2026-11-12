#include "presentation/commands/kiotty_commands.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace kiotty::cli;
using Check = std::function<bool()>;

constexpr KiottySpecCheckpointHandle LARGEST_CHECKPOINT_NUMBER = 999999999;
constexpr KiottySpecCheckpointHandle CURRENT_CHECKPOINT = KIOTTY_SPEC_CHECKPOINT_CURRENT;
const std::string INVALID_UTF8_SUFFIX = "\xff";

std::string appendNullByte(const std::string& text)
{
    std::string joined = text;
    joined.push_back('\0');
    return joined + "1";
}

bool expectTrue(bool condition, const char* label)
{
    if (!condition) { std::printf("  FAIL: %s\n", label); }
    return condition;
}

bool testParseNumber()
{
    const std::optional<uint32_t> empty = parseNumber("");
    const std::optional<uint32_t> largest = parseNumber("4294967295");
    const std::optional<uint32_t> too_large = parseNumber("4294967296");
    const std::optional<uint32_t> with_null = parseNumber(appendNullByte("1"));
    const std::optional<uint32_t> with_bad_text = parseNumber("1" + INVALID_UTF8_SUFFIX);
    bool passed = true;
    passed = expectTrue(!empty, "empty number is rejected") && passed;
    passed = expectTrue(largest == UINT32_MAX, "largest number is accepted") && passed;
    passed = expectTrue(!too_large, "number past the largest is rejected") && passed;
    passed = expectTrue(!with_null, "number with null byte is rejected") && passed;
    passed = expectTrue(!with_bad_text, "number with bad UTF-8 is rejected") && passed;
    return passed;
}

bool testParseActor()
{
    const std::optional<KiottySpecActor> empty = parseActor("");
    const std::optional<KiottySpecActor> longest = parseActor("user");
    const std::optional<KiottySpecActor> too_long = parseActor("users");
    const std::optional<KiottySpecActor> with_null = parseActor(appendNullByte("user"));
    const std::optional<KiottySpecActor> with_bad_text = parseActor("ai" + INVALID_UTF8_SUFFIX);
    bool passed = true;
    passed = expectTrue(empty == KIOTTY_SPEC_ACTOR_AI, "empty actor is ai") && passed;
    passed = expectTrue(longest == KIOTTY_SPEC_ACTOR_USER, "user actor is accepted") && passed;
    passed = expectTrue(!too_long, "longer actor is rejected") && passed;
    passed = expectTrue(!with_null, "actor with null byte is rejected") && passed;
    passed = expectTrue(!with_bad_text, "actor with bad UTF-8 is rejected") && passed;
    passed = expectTrue(parseActor("ai") == KIOTTY_SPEC_ACTOR_AI, "ai actor is accepted") && passed;
    return passed;
}

bool testParseCheckpoint()
{
    const KiottySpecCheckpointHandle seventh {7};
    const std::optional<KiottySpecCheckpointHandle> empty = parseCheckpoint("");
    const std::optional<KiottySpecCheckpointHandle> largest = parseCheckpoint("CP-999999999");
    const std::optional<KiottySpecCheckpointHandle> padded = parseCheckpoint("CP-0007");
    const std::optional<KiottySpecCheckpointHandle> too_long = parseCheckpoint("CP-1000000000");
    const std::optional<KiottySpecCheckpointHandle> with_null =
        parseCheckpoint(appendNullByte("CP-1"));
    const std::optional<KiottySpecCheckpointHandle> with_bad_text =
        parseCheckpoint("CP-1" + INVALID_UTF8_SUFFIX);
    bool passed = true;
    passed = expectTrue(!empty, "empty checkpoint is rejected") && passed;
    passed = expectTrue(largest == LARGEST_CHECKPOINT_NUMBER, "nine digits are accepted") && passed;
    passed = expectTrue(padded == seventh, "padded checkpoint is accepted") && passed;
    passed = expectTrue(!too_long, "ten digits are rejected") && passed;
    passed = expectTrue(!with_null, "null byte is rejected") && passed;
    passed = expectTrue(!with_bad_text, "bad UTF-8 is rejected") && passed;
    return passed;
}

bool testParseCheckpointArgument()
{
    const std::optional<KiottySpecCheckpointHandle> current = parseCheckpointArgument("current");
    const std::optional<KiottySpecCheckpointHandle> dash = parseCheckpointArgument("-");
    const std::optional<KiottySpecCheckpointHandle> largest =
        parseCheckpointArgument("CP-999999999");
    const std::optional<KiottySpecCheckpointHandle> too_long =
        parseCheckpointArgument("CP-1000000000");
    const std::optional<KiottySpecCheckpointHandle> with_null =
        parseCheckpointArgument(appendNullByte("current"));
    const std::optional<KiottySpecCheckpointHandle> with_bad_text =
        parseCheckpointArgument("current" + INVALID_UTF8_SUFFIX);
    bool passed = true;
    passed = expectTrue(!parseCheckpointArgument(""), "empty argument is rejected") && passed;
    passed = expectTrue(current == CURRENT_CHECKPOINT, "current is accepted") && passed;
    passed = expectTrue(dash == CURRENT_CHECKPOINT, "dash is accepted") && passed;
    passed = expectTrue(largest == LARGEST_CHECKPOINT_NUMBER, "nine digits are accepted") && passed;
    passed = expectTrue(!too_long, "ten digits are rejected") && passed;
    passed = expectTrue(!with_null, "null byte is rejected") && passed;
    passed = expectTrue(!with_bad_text, "bad UTF-8 is rejected") && passed;
    return passed;
}

bool testParseTagSpec()
{
    const std::string longest_name(KIOTTY_SPEC_MAX_TAG_NAME_LENGTH, 'a');
    const std::optional<TagSpecText> longest = parseTagSpec(longest_name + "=open");
    const std::optional<TagSpecText> too_long = parseTagSpec(longest_name + "a=open");
    const std::optional<TagSpecText> name_only = parseTagSpec("status");
    const std::optional<TagSpecText> with_null = parseTagSpec(appendNullByte("status") + "=open");
    const std::optional<TagSpecText> with_bad_text = parseTagSpec("st" + INVALID_UTF8_SUFFIX);
    const bool is_longest_kept = longest.has_value() && longest.value().name == longest_name &&
                                 longest.value().bits == std::string("open");
    const bool is_name_only = name_only.has_value() && !name_only.value().bits.has_value();
    const bool is_bad_text_kept =
        with_bad_text.has_value() && with_bad_text.value().name == "st" + INVALID_UTF8_SUFFIX;
    bool passed = true;
    passed = expectTrue(!parseTagSpec(""), "empty tag spec is rejected") && passed;
    passed = expectTrue(!parseTagSpec("=open"), "tag spec without a name is rejected") && passed;
    passed = expectTrue(is_longest_kept, "longest tag name is accepted") && passed;
    passed = expectTrue(!too_long, "tag name past the longest is rejected") && passed;
    passed = expectTrue(is_name_only, "tag name without bits is accepted") && passed;
    passed = expectTrue(!with_null, "tag name with null byte is rejected") && passed;
    passed = expectTrue(is_bad_text_kept, "bad UTF-8 name is left to the model lookup") && passed;
    return passed;
}

std::unique_ptr<KiottySpecTagDefinition> makeTagDefinition(const std::string& longest_name)
{
    std::unique_ptr<KiottySpecTagDefinition> definition =
        std::make_unique<KiottySpecTagDefinition>();
    std::memcpy(definition->name, "status", sizeof("status"));
    std::memcpy(definition->bit_names[0], longest_name.c_str(), longest_name.size() + 1);
    std::memcpy(definition->bit_names[1], "open", sizeof("open"));
    return definition;
}

bool testParseBits()
{
    const std::string longest_name(KIOTTY_SPEC_MAX_TAG_NAME_LENGTH, 'a');
    const std::unique_ptr<KiottySpecTagDefinition> storage = makeTagDefinition(longest_name);
    const KiottySpecTagDefinition& definition = *storage;
    const std::optional<uint64_t> longest = parseBits(definition, longest_name);
    const std::optional<uint64_t> both = parseBits(definition, longest_name + ",open");
    const std::optional<uint64_t> cleared = parseBits(definition, "-");
    const std::optional<uint64_t> empty = parseBits(definition, "");
    const std::optional<uint64_t> too_long = parseBits(definition, longest_name + "a");
    const std::optional<uint64_t> with_null = parseBits(definition, appendNullByte("open"));
    const std::optional<uint64_t> with_bad_text =
        parseBits(definition, "open" + INVALID_UTF8_SUFFIX);
    bool passed = true;
    passed = expectTrue(!empty, "empty bits are rejected") && passed;
    passed = expectTrue(longest == uint64_t {1}, "longest bit name is accepted") && passed;
    passed = expectTrue(both == uint64_t {3}, "two bit names are combined") && passed;
    passed = expectTrue(cleared == uint64_t {0}, "dash clears the bits") && passed;
    passed = expectTrue(!too_long, "longer bit name is rejected") && passed;
    passed = expectTrue(!with_null, "null byte is rejected") && passed;
    passed = expectTrue(!with_bad_text, "bad UTF-8 bit name is rejected") && passed;
    return passed;
}

bool testParseArgumentsEmpty()
{
    const Arguments arguments = parseArguments({}, 0, {});
    const bool is_empty = arguments.positional.empty() && arguments.options.empty() &&
                          arguments.flags.empty();
    const Arguments blank = parseArguments({""}, 0, {});
    const bool is_blank_positional = blank.positional == std::vector<std::string> {""};
    bool passed = true;
    passed = expectTrue(is_empty, "no tokens give no arguments") && passed;
    passed = expectTrue(is_blank_positional, "empty token stays positional") && passed;
    return passed;
}

bool testParseArgumentsLength()
{
    const std::string longest(KIOTTY_SPEC_MAX_BODY_LENGTH, 'b');
    const std::string too_long = longest + "b";
    const Arguments arguments =
        parseArguments({"item", "--body", longest, "--summary", too_long}, 1, {});
    const bool is_longest_kept = arguments.option("body") == longest;
    const bool is_too_long_kept = arguments.option("summary") == too_long;
    bool passed = true;
    passed = expectTrue(is_longest_kept, "longest option value is kept") && passed;
    passed = expectTrue(is_too_long_kept, "longer option value is left to the library") && passed;
    return passed;
}

bool testParseArgumentsBytes()
{
    const std::string with_null = appendNullByte("title");
    const std::string with_bad_text = "title" + INVALID_UTF8_SUFFIX;
    const Arguments arguments =
        parseArguments({"--title", with_null, with_bad_text, "--tags"}, 0, {});
    const bool is_null_kept = arguments.option("title") == with_null;
    const bool is_bad_text_kept = arguments.positional == std::vector<std::string> {with_bad_text};
    const bool has_flag = arguments.flag("tags");
    bool passed = true;
    passed = expectTrue(is_null_kept, "null byte in an option value is kept") && passed;
    passed = expectTrue(is_bad_text_kept, "bad UTF-8 positional is left to the library") && passed;
    passed = expectTrue(has_flag, "known flag takes no value") && passed;
    return passed;
}

}

int main()
{
    const std::pair<const char*, Check> tests[] = {
        {"parse number", testParseNumber},
        {"parse actor", testParseActor},
        {"parse checkpoint", testParseCheckpoint},
        {"parse checkpoint argument", testParseCheckpointArgument},
        {"parse tag spec", testParseTagSpec},
        {"parse bits", testParseBits},
        {"parse arguments empty", testParseArgumentsEmpty},
        {"parse arguments length", testParseArgumentsLength},
        {"parse arguments bytes", testParseArgumentsBytes},
    };

    int failed = 0;
    for (const auto& test : tests) {
        const bool passed = test.second();
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", test.first);
        if (!passed) { ++failed; }
    }
    std::printf("%d failed\n", failed);
    return failed;
}
