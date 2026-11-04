#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kiotty::core {

size_t utf8Length(std::string_view text);                       // 코드포인트 수
std::string asciiLower(std::string_view text);
bool containsInsensitive(std::string_view haystack, std::string_view needle); // ASCII 만 대소문자 무시
std::string_view trim(std::string_view text);
std::vector<std::string_view> splitLines(std::string_view text);
std::string escapeLike(std::string_view text);                  // SQL LIKE 의 %, _ 이스케이프 ('\\')
std::string numberedId(std::string_view prefix, int64_t number, int width);

std::filesystem::path pathFromUtf8(std::string_view text);
std::string pathToUtf8(const std::filesystem::path &path);

std::optional<std::string> readFile(const std::filesystem::path &path);
void writeFile(const std::filesystem::path &path, std::string_view content);

std::string fnv1a64Hex(std::string_view data);
int64_t nowEpochSeconds();

} // namespace kiotty::core
