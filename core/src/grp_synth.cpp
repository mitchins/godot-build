#include "fauxbuild/grp_synth.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#include "fauxbuild/check.hpp"

namespace fauxbuild::synth {

namespace {

std::uint32_t next(std::uint32_t& state) {
    // Numerical Recipes LCG; deterministic and platform-independent.
    state = state * 1664525u + 1013904223u;
    return state;
}

std::uint8_t byte(std::uint32_t& state) {
    return static_cast<std::uint8_t>(next(state) >> 24);
}

void put_u32_le(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 24));
}

} // namespace

std::vector<std::uint8_t> generate_grp(const GrpSpec& spec) {
    std::uint32_t state = spec.seed;

    struct Layout {
        std::string name;
        std::uint32_t size;
        std::uint32_t content_seed;
    };

    std::vector<Layout> layout;
    layout.reserve(spec.file_count);
    for (std::uint32_t i = 0; i < spec.file_count; ++i) {
        char name[16];
        // "SYN0000.DAT" is 11 chars: fits the 12-byte GRP name field with NUL.
        std::snprintf(name, sizeof(name), "SYN%04u.DAT", i);
        // uint64 range: max_file_size + 1 wraps to zero at UINT32_MAX in uint32.
        const std::uint64_t range = static_cast<std::uint64_t>(spec.max_file_size) + 1;
        std::uint32_t size = static_cast<std::uint32_t>(next(state) % range);
        if (spec.include_zero_size && i % 5 == 0) {
            size = 0;
        }
        layout.push_back({name, size, next(state)});
    }

    // Reserve the exact image size from the layout that was just computed. The
    // previous estimate scaled with max_file_size, so a large bound requested a
    // huge allocation regardless of the sizes actually generated (UINT32_MAX
    // asked for ~34 GB and threw bad_alloc on Linux and MSVC).
    std::uint64_t data_total = 0;
    for (const auto& file : layout) {
        data_total += file.size;
    }
    std::vector<std::uint8_t> out;
    out.reserve(16 + 16ull * spec.file_count + data_total);
    const char* signature = "KenSilverman";
    out.insert(out.end(), signature, signature + 12);

    put_u32_le(out, spec.file_count);

    for (const auto& file : layout) {
        char field[12] = {};
        const std::size_t name_len = file.name.size() > 12 ? 12 : file.name.size();
        std::memcpy(field, file.name.c_str(), name_len);
        out.insert(out.end(), field, field + 12);
        put_u32_le(out, file.size);
    }

    for (auto& file : layout) {
        state = file.content_seed;
        for (std::uint32_t i = 0; i < file.size; ++i) {
            out.push_back(byte(state));
        }
    }

    return out;
}

std::vector<std::uint8_t> build_grp(const std::vector<GrpFileSpec>& files) {
    std::vector<std::uint8_t> out;
    std::uint64_t payload = 0;
    for (const auto& file : files) {
        // Content-safety contract (our own tooling input): names must fit
        // the 12-byte directory field and contain no separators or NULs.
        FB_CHECK(file.name.size() <= 12);
        FB_CHECK(file.name.find('/') == std::string::npos);
        FB_CHECK(file.name.find('\\') == std::string::npos);
        FB_CHECK(file.name.find('\0') == std::string::npos);
        FB_CHECK(!file.name.empty());
        payload += file.bytes.size();
    }
    out.reserve(static_cast<std::size_t>(16 + 16ull * files.size() + payload));

    out.insert(out.end(), {'K', 'e', 'n', 'S', 'i', 'l', 'v', 'e', 'r', 'm', 'a', 'n'});
    put_u32_le(out, static_cast<std::uint32_t>(files.size()));
    for (const auto& file : files) {
        std::string upper;
        upper.reserve(file.name.size());
        for (const char c : file.name) {
            upper.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
        }
        std::array<std::uint8_t, 12> name{};
        std::memcpy(name.data(), upper.data(), upper.size()); // NUL-padded
        out.insert(out.end(), name.begin(), name.end());
        put_u32_le(out, static_cast<std::uint32_t>(file.bytes.size()));
    }
    for (const auto& file : files) {
        out.insert(out.end(), file.bytes.begin(), file.bytes.end());
    }
    return out;
}

} // namespace fauxbuild::synth
