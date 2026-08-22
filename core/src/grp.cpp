#include "fauxbuild/grp.hpp"

#include <algorithm>
#include <cstring>
#include <set>

#include "fauxbuild/byte_reader.hpp"

namespace fauxbuild::grp {

namespace {

std::string to_key(const std::string& name) {
    std::string key;
    key.reserve(name.size());
    for (const char c : name) {
        key.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
    }
    return key;
}

bool is_legal_flat_name(const std::string& name) {
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    return name.find('/') == std::string::npos && name.find('\\') == std::string::npos;
}

} // namespace

Result<GrpData> parse(std::string_view image, std::string source, GrpDiagnostics* diags) {
    ByteReader reader(reinterpret_cast<const std::uint8_t*>(image.data()), image.size(), source);

    auto signature = reader.read_bytes(12);
    if (!signature.is_ok()) {
        return Result<GrpData>::err(signature.error());
    }
    if (std::memcmp(signature.value().data, "KenSilverman", 12) != 0) {
        return Result<GrpData>::err(
            {source, 0, "grp.header", ErrorCode::BadSignature, "not a GRP container"});
    }

    auto file_count = reader.read_u32_le();
    if (!file_count.is_ok()) {
        return Result<GrpData>::err(file_count.error());
    }
    GrpData data;
    data.file_count = file_count.value();
    data.data_start = 16 + 16ull * data.file_count; // 12 signature + 4 count

    const auto image_size = static_cast<std::uint64_t>(image.size());
    if (data.data_start > image_size) {
        return Result<GrpData>::err({source, image_size < 16 ? image_size : 16, "grp.directory",
                                     ErrorCode::OutOfBounds,
                                     "directory needs 16 + 16*" + std::to_string(data.file_count) +
                                         " = " + std::to_string(data.data_start) +
                                         " bytes but container has " + std::to_string(image_size)});
    }

    std::set<std::string> seen_keys;
    std::uint64_t cursor = data.data_start;
    // Clamp the speculative reserve. file_count is bounded by the directory-fits
    // check above, but GrpEntry is 64 bytes against 16 bytes of directory, so a
    // declared count still amplifies 4x: a 1 GiB image (read_file_bytes' default
    // cap) would reserve 4 GiB before a single entry is validated. Beyond the
    // clamp the vector grows as entries actually parse, and every parsed entry
    // is paid for by 16 real bytes in the image.
    constexpr std::uint32_t kEntryReserveClamp = 4096;
    data.entries.reserve(std::min(data.file_count, kEntryReserveClamp));
    for (std::uint32_t i = 0; i < data.file_count; ++i) {
        const auto record = "grp.directory[" + std::to_string(i) + "]";
        auto name_bytes = reader.read_bytes(12);
        if (!name_bytes.is_ok()) {
            return Result<GrpData>::err(name_bytes.error());
        }
        auto entry_size = reader.read_u32_le();
        if (!entry_size.is_ok()) {
            return Result<GrpData>::err(entry_size.error());
        }

        const char* raw = reinterpret_cast<const char*>(name_bytes.value().data);
        std::string name(raw, ::strnlen(raw, 12));
        if (!is_legal_flat_name(name)) {
            return Result<GrpData>::err({source, 16 + 16ull * i, record, ErrorCode::InvalidName,
                                         "illegal file name in directory entry"});
        }
        const std::string key = to_key(name);
        if (!seen_keys.insert(key).second && diags) {
            diags->warnings.push_back("duplicate name " + key + " in grp.directory[" +
                                      std::to_string(i) + "]; first entry wins");
        }

        GrpEntry entry;
        entry.name = std::move(name);
        entry.key = std::move(key);
        entry.size = entry_size.value();
        entry.offset = cursor;
        cursor += entry.size;
        if (cursor > image_size) {
            return Result<GrpData>::err(
                {source, entry.offset, record + ".data", ErrorCode::OutOfBounds,
                 "file data (offset " + std::to_string(entry.offset) + ", size " +
                     std::to_string(entry.size) + ") exceeds container size " +
                     std::to_string(image_size)});
        }
        data.entries.push_back(std::move(entry));
    }

    return Result<GrpData>::ok(std::move(data));
}

} // namespace fauxbuild::grp
