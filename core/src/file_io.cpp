#include "fauxbuild/file_io.hpp"

#include <cstdio>

namespace fauxbuild {

Result<std::vector<std::uint8_t>> read_file_bytes(const std::string& path,
                                                  std::uint64_t max_bytes) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        return Result<std::vector<std::uint8_t>>::err(
            {path, 0, "file", ErrorCode::IoError, "cannot open for reading"});
    }

    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return Result<std::vector<std::uint8_t>>::err(
            {path, 0, "file", ErrorCode::IoError, "cannot seek to end"});
    }
    const long length = std::ftell(file);
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
    const bool ok = written == size && std::fclose(file) == 0;
    if (!ok) {
        return Result<std::size_t>::err({path, 0, "file", ErrorCode::IoError, "short write"});
    }
    return Result<std::size_t>::ok(written);
}

} // namespace fauxbuild
