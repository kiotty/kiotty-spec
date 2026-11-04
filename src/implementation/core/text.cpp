#include "implementation/core/text.h"

#include "implementation/core/error.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>

namespace kiotty::core {

size_t utf8Length(std::string_view text) {
    size_t count = 0;
    for (const char c : text) {
        if ((static_cast<unsigned char>(c) & 0xC0U) != 0x80U) {
            ++count;
        }
    }
    return count;
}

std::string asciiLower(std::string_view text) {
    std::string result(text);
    for (char &c : result) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return result;
}

bool containsInsensitive(std::string_view haystack, std::string_view needle) {
    return asciiLower(haystack).find(asciiLower(needle)) != std::string::npos;
}

std::string_view trim(std::string_view text) {
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!text.empty() && isSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            if (start < text.size()) {
                lines.push_back(text.substr(start));
            }
            break;
        }
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        start = end + 1;
    }
    return lines;
}

std::string escapeLike(std::string_view text) {
    std::string result;
    for (const char c : text) {
        if (c == '%' || c == '_' || c == '\\') {
            result += '\\';
        }
        result += c;
    }
    return result;
}

std::string numberedId(std::string_view prefix, int64_t number, int width) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%0*lld", width, static_cast<long long>(number));
    return std::string(prefix) + "-" + buffer;
}

std::filesystem::path pathFromUtf8(std::string_view text) {
    const std::u8string u8(reinterpret_cast<const char8_t *>(text.data()), text.size());
    return std::filesystem::path(u8);
}

std::string pathToUtf8(const std::filesystem::path &path) {
    const std::u8string u8 = path.generic_u8string();
    return std::string(reinterpret_cast<const char *>(u8.data()), u8.size());
}

std::optional<std::string> readFile(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

void writeFile(const std::filesystem::path &path, std::string_view content) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        fail(KiottySpecStatus_Io, "파일을 쓸 수 없음: " + pathToUtf8(path));
    }
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string fnv1a64Hex(std::string_view data) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (const char c : data) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 0x100000001b3ULL;
    }
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
    return buffer;
}

int64_t nowEpochSeconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace kiotty::core
