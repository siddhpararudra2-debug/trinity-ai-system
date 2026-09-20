#pragma once

// Result<T>: value-or-error return type for fallible core operations.
// Success carries a value; failure carries an ErrorInfo. No exceptions
// for expected failure modes at this layer (exceptions remain for
// truly exceptional contract violations).

#include <optional>
#include <utility>

#include "Error.hpp"

namespace trinity::core {

template <typename T>
class Result {
public:
    static Result ok(T value) { return Result(std::move(value)); }
    static Result fail(ErrorInfo error) { return Result(std::move(error)); }

    bool isOk() const noexcept { return value_.has_value(); }
    const T& value() const { return *value_; }
    T& value() { return *value_; }
    const ErrorInfo& error() const { return *error_; }

private:
    explicit Result(T value) : value_(std::move(value)) {}
    explicit Result(ErrorInfo error) : error_(std::move(error)) {}

    std::optional<T> value_;
    std::optional<ErrorInfo> error_;
};

using Status = Result<bool>;

inline Status okStatus() { return Status::ok(true); }

inline Status errStatus(ErrorCode code, std::string message, Json details = Json::object(),
                        std::string source = "") {
    return Status::fail(makeError(code, std::move(message), std::move(source),
                                  std::move(details)));
}

}  // namespace trinity::core
