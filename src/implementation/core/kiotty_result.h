#pragma once

#include <type_traits>
#include <utility>

namespace kiotty::spec::implementation {

template <typename Value, typename Code>
struct Result
{
    static_assert(std::is_enum<Code>::value, "Code must be an enum class");
    static_assert(std::is_default_constructible<Value>::value,
                  "Value must be default constructible");

    Code code {};
    Value value {};

    explicit operator bool() const { return code == Code{}; }

    static Result success(Value content) { return Result{Code{}, std::move(content)}; }
    static Result failure(Code error_code) { return Result{error_code, Value{}}; }
};

template <typename Value, typename Code>
struct Result<Value&, Code>
{
    static_assert(std::is_enum<Code>::value, "Code must be an enum class");

    Code code {};
    Value* value {};

    explicit operator bool() const { return code == Code{}; }

    static Result success(Value& content) { return Result{Code{}, &content}; }
    static Result failure(Code error_code) { return Result{error_code, nullptr}; }
};

}
