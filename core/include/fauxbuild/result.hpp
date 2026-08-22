#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <variant>

namespace fauxbuild {

enum class ErrorCode {
    Truncated,
    BadSignature,
    OutOfBounds,
    InvalidName,
    PathTraversal,
    NotFound,
    TooLarge,
    IoError,
    Unsupported,
};

const char* error_code_name(ErrorCode code);

// Structured error for untrusted-input failures (plan §6.3). Every parser must
// report all five fields. This is the error half of D0006's boundary: malformed
// content produces ParseError values — FB_CHECK is never used on external data.
struct ParseError {
    std::string source;       // origin file/mount the bytes came from
    std::uint64_t offset = 0; // byte offset where the problem was detected
    std::string record;       // record kind/index, e.g. "grp.directory[3]"
    ErrorCode code = ErrorCode::Truncated;
    std::string detail; // human-readable explanation

    std::string to_string() const;
};

template <typename T> class Result {
  public:
    static Result ok(T value) { return Result(std::move(value)); }
    static Result err(ParseError error) { return Result(std::move(error)); }

    bool is_ok() const { return std::holds_alternative<T>(v_); }
    explicit operator bool() const { return is_ok(); }

    const T& value() const& { return std::get<T>(v_); }
    T& value() & { return std::get<T>(v_); }
    T take() { return std::get<T>(std::move(v_)); }

    const ParseError& error() const { return std::get<ParseError>(v_); }

  private:
    explicit Result(T value) : v_(std::move(value)) {}
    explicit Result(ParseError error) : v_(std::move(error)) {}

    std::variant<T, ParseError> v_;
};

template <> class Result<void> {
  public:
    static Result ok() { return Result(); }
    static Result err(ParseError error) { return Result(std::move(error)); }

    bool is_ok() const { return ok_; }
    explicit operator bool() const { return ok_; }

    const ParseError& error() const { return err_; }

  private:
    Result() = default;
    explicit Result(ParseError error) : ok_(false), err_(std::move(error)) {}

    bool ok_ = true;
    ParseError err_;
};

} // namespace fauxbuild
