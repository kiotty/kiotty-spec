#pragma once

#include <map>
#include <optional>
#include <stdexcept>
#include <set>
#include <string>
#include <vector>

namespace kiotty::cli {

// 명령줄: "ks <그룹> <동사> [위치 인자...] [--옵션 값...] [--플래그...]"
class Arguments {
public:
    explicit Arguments(std::vector<std::string> tokens);

    const std::vector<std::string> &positional() const { return positional_; }
    std::string at(size_t index, const char *what) const;            // 없으면 사용법 오류
    std::optional<std::string> optionalAt(size_t index) const;

    std::optional<std::string> option(const std::string &name) const;
    std::string required(const std::string &name) const;
    int intOption(const std::string &name, int fallback) const;
    bool flag(const std::string &name) const;

    // --<name> 값, --<name>-file 경로, --<name> - (표준 입력) 중 하나로 본문을 받는다.
    std::optional<std::string> text(const std::string &name) const;

    void requireNoUnknown(const std::set<std::string> &known) const;

private:
    std::vector<std::string> positional_;
    std::map<std::string, std::string> options_;
    std::set<std::string> flags_;
};

class UsageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

int toInt(const std::string &text, const char *what);

} // namespace kiotty::cli
