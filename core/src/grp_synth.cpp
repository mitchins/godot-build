#include "fauxbuild/grp_synth.hpp"

#include <string>

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
        std::uint32_t size = next(state) % (spec.max_file_size + 1);
        if (spec.include_zero_size && i % 5 == 0) {
            size = 0;
        }
        layout.push_back({name, size, next(state)});
    }

    std::vector<std::uint8_t> out;
    out.reserve(16 + 16ull * spec.file_count + 16ull * spec.max_file_size / 2);
    const char* signature = "KenSilverman";
    out.insert(out.end(), signature, signature + 12);

    std::uint64_t total = 0;
    for (const auto& file : layout) {
        total += file.size;
    }
    put_u32_le(out, spec.file_count);
    put_u32_le(out, static_cast<std::uint32_t>(total));

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

} // namespace fauxbuild::synth
