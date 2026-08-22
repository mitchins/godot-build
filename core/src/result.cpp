#include "fauxbuild/result.hpp"

namespace fauxbuild {

const char* error_code_name(ErrorCode code) {
    switch (code) {
    case ErrorCode::Truncated:
        return "truncated";
    case ErrorCode::BadSignature:
        return "bad_signature";
    case ErrorCode::OutOfBounds:
        return "out_of_bounds";
    case ErrorCode::InvalidName:
        return "invalid_name";
    case ErrorCode::PathTraversal:
        return "path_traversal";
    case ErrorCode::NotFound:
        return "not_found";
    case ErrorCode::TooLarge:
        return "too_large";
    case ErrorCode::IoError:
        return "io_error";
    case ErrorCode::Unsupported:
        return "unsupported";
    }
    return "unknown";
}

std::string ParseError::to_string() const {
    std::string text;
    text.reserve(96 + record.size() + detail.size() + source.size());
    text += error_code_name(code);
    text += " at ";
    text += source;
    text += "+";
    text += std::to_string(offset);
    text += " [";
    text += record;
    text += "]: ";
    text += detail;
    return text;
}

} // namespace fauxbuild
