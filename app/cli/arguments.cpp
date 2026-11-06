#include "arguments.h"
#include "path_utf8.h"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace kiotty::cli {

namespace {

const std::set<std::string> kFlags{"hooks", "recursive", "merge", "help"};

std::string readStream(std::istream &stream) {
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

}

int toInt(const std::string &text, const char *what) {
    int value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || ptr != text.data() + text.size()) {
        throw UsageError(std::string(what) + " 은 정수여야 함: '" + text + "'");
    }
    return value;
}

Arguments::Arguments(std::vector<std::string> tokens) {
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string &token = tokens[i];
        if (token.size() > 2 && token.rfind("--", 0) == 0) {
            std::string name = token.substr(2);
            const size_t eq = name.find('=');
            if (eq != std::string::npos) {
                options_[name.substr(0, eq)] = name.substr(eq + 1);
            } else if (kFlags.count(name) != 0) {
                flags_.insert(name);
            } else {
                if (i + 1 >= tokens.size()) {
                    throw UsageError("--" + name + " 에 값이 없음");
                }
                options_[name] = tokens[++i];
            }
        } else {
            positional_.push_back(token);
        }
    }
}

std::string Arguments::at(size_t index, const char *what) const {
    if (index >= positional_.size()) {
        throw UsageError(std::string(what) + " 이 필요함");
    }
    return positional_[index];
}

std::optional<std::string> Arguments::optionalAt(size_t index) const {
    if (index >= positional_.size()) return std::nullopt;
    return positional_[index];
}

std::optional<std::string> Arguments::option(const std::string &name) const {
    const auto it = options_.find(name);
    if (it == options_.end()) return std::nullopt;
    return it->second;
}

std::string Arguments::required(const std::string &name) const {
    auto value = option(name);
    if (!value) throw UsageError("--" + name + " 이 필요함");
    return *value;
}

int Arguments::intOption(const std::string &name, int fallback) const {
    const auto value = option(name);
    return value ? toInt(*value, ("--" + name).c_str()) : fallback;
}

bool Arguments::flag(const std::string &name) const { return flags_.count(name) != 0; }

std::optional<std::string> Arguments::text(const std::string &name) const {
    if (const auto path = option(name + "-file")) {
        std::ifstream stream(pathFromUtf8(*path), std::ios::binary);
        if (!stream) throw UsageError("파일을 읽을 수 없음: " + *path);
        return readStream(stream);
    }
    const auto value = option(name);
    if (value && *value == "-") return readStream(std::cin);
    return value;
}

void Arguments::requireNoUnknown(const std::set<std::string> &known) const {
    for (const auto &[name, value] : options_) {
        (void)value;
        const std::string base = name.size() > 5 && name.compare(name.size() - 5, 5, "-file") == 0 ? name.substr(0, name.size() - 5) : name;
        if (known.count(name) == 0 && known.count(base) == 0) throw UsageError("알 수 없는 옵션 --" + name);
    }
}

}
