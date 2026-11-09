#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kiotty::spec::implementation {

size_t utf8Length(std::string_view text);
std::string asciiLower(std::string_view text);
bool containsInsensitive(std::string_view haystack, std::string_view needle);
std::string_view trim(std::string_view text);
std::vector<std::string_view> splitLines(std::string_view text);
std::string escapeLike(std::string_view text);
std::string numberedId(std::string_view prefix, int64_t number, int width);

std::string fnv1a64Hex(std::string_view data);
int64_t nowEpochSeconds();

}
