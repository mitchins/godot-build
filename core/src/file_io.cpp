#include "fauxbuild/file_io.hpp"

#include <cstdio>

// 64-bit file offsets: std::ftell/std::fseek use 32-bit long on MSVC, so files
// above 2 GiB would fail with IoError before the max_bytes check can report
// TooLarge. Route through the platform's 64-bit equivalents.
#if defined(_WIN32)
#define FB_FSEEK _fseeki64
#define FB_FTELL _ftelli64
using FbOffset = __int64;
#else
#include <sys/types.h>
#define FB_FSEEK fseeko
#define FB_FTELL ftello
using FbOffset = off_t;
#endif

namespace fauxbuild {

Result<std::vector<std::uint8_t>> read_file_bytes(const std::string& path,
                                                  std::uint64_t max_bytes) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        return Result<std::vector<std::uint8_t>>::err(
            {path, 0, "file", ErrorCode::IoError, "cannot open for reading"});
    }

    if (FB_FSEEK(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return Result<std::vector<std::uint8_t>>::err(
            {path, 0, "file", ErrorCode::IoError, "cannot seek to end"});
    }
    const FbOffset length = FB_FTELL(file);
    if (length < 0) {
        std::fclose(file);
        return Result<std::vector<std::uint8_t>>::err(
            {path, 0, "file", ErrorCode::IoError, "cannot determine size"});
    }
    if (static_cast<std::uint64_t>(length) > max_bytes) {
        std::fclose(file);
        return Result<std::vector<std::uint8_t>>::err(
            {path, 0, "file", ErrorCode::TooLarge,
             "size " + std::to_string(length) + " exceeds limit " + std::to_string(max_bytes)});
    }
    std::rewind(file);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (length > 0 && std::fread(bytes.data(), 1, static_cast<std::size_t>(length), file) !=
                          static_cast<std::size_t>(length)) {
        std::fclose(file);
        return Result<std::vector<std::uint8_t>>::err(
            {path, 0, "file", ErrorCode::IoError, "short read"});
    }
    std::fclose(file);
    return Result<std::vector<std::uint8_t>>::ok(std::move(bytes));
}

Result<std::size_t> write_file_bytes(const std::string& path, const std::uint8_t* data,
                                     std::size_t size) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        return Result<std::size_t>::err(
            {path, 0, "file", ErrorCode::IoError, "cannot open for writing"});
    }
    const std::size_t written = size == 0 ? 0 : std::fwrite(data, 1, size, file);
    // fclose unconditionally: a && short-circuit leaked the handle on every
    // partial-write path.
    const bool closed = std::fclose(file) == 0;
    if (written != size || !closed) {
        return Result<std::size_t>::err(
            {path, 0, "file", ErrorCode::IoError,
             written != size ? "short write" : "close failed after write"});
    }
    return Result<std::size_t>::ok(written);
}

} // namespace fauxbuild
