// Trinity — Result<T>: explicit error propagation for public APIs.
// Internal helpers may throw TrinityException; every public entry point
// converts to Result so callers never need try/catch for flow control.
#pragma once

#include <optional>
#include <utility>
#include <variant>

#include "Error.hpp"

namespace trinity::core {

template <typename T>
class Result {
public:
    static Result ok(T value) { return Result(std::in_place_index<0>, std::move(value)); }
    static Result fail(Error error) { return Result(std::in_place_index<1>, std::move(error)); }

    bool is_ok() const { return result_.index() == 0; }
    bool is_error() const { return result_.index() == 1; }

    T& value() { return std::get<0>(result_); }
    const T& value() const { return std::get<0>(result_); }
    T take_value() { return std::move(std::get<0>(result_)); }

    const Error& error() const { return std::get<1>(result_); }
    Error take_error() { return std::move(std::get<1>(result_)); }

    T value_or(T fallback) const {
        return is_ok() ? std::get<0>(result_) : std::move(fallback);
    }

private:
    template <std::size_t I, typename V>
    Result(std::in_place_index_t<I> tag, V&& v) : result_(tag, std::forward<V>(v)) {}

    std::variant<T, Error> result_;
};

// Result for operations that only report success/failure.
class Status {
public:
    static Status ok() { return Status(std::nullopt); }
    static Status fail(Error error) { return Status(std::move(error)); }

    bool is_ok() const { return !error_.has_value(); }
    const Error& error() const { return *error_; }
    Error take_error() { return std::move(*error_); }

private:
    explicit Status(std::optional<Error> error) : error_(std::move(error)) {}
    std::optional<Error> error_;
};

// Collapses a Result<T> into a Status for call sites that only report success
// or failure (e.g. a database write whose affected-row count is irrelevant).
template <typename T>
Status status_of(Result<T>&& outcome) {
    if (outcome.is_ok()) return Status::ok();
    return Status::fail(outcome.take_error());
}

}  // namespace trinity::core
