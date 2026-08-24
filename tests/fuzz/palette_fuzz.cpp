// Fuzz target: PALETTE.DAT / LOOKUP.DAT over untrusted bytes. Invariants
// beyond no-crash: parse success -> canonical write -> byte-identical input
// (both formats preserve every blob verbatim, so anything that parses must
// re-serialize exactly). Findings become committed regression inputs (D0010).
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include "fauxbuild/palette.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view bytes(reinterpret_cast<const char*>(data), size);

    auto palette = fauxbuild::read_palette_dat(bytes, "fuzz-pal");
    if (palette.is_ok()) {
        auto written = fauxbuild::write_palette_dat(palette.value());
        if (!written.is_ok()) {
            __builtin_trap(); // our bug: parsed a palette we refuse to write
        }
        if (written.value().size() != size ||
            std::memcmp(written.value().data(), data, size) != 0) {
            __builtin_trap(); // our bug: canonical write is not byte-identical
        }
    }

    auto lookup = fauxbuild::read_lookup_dat(bytes, "fuzz-lut");
    if (lookup.is_ok()) {
        auto written = fauxbuild::write_lookup_dat(lookup.value());
        if (!written.is_ok()) {
            __builtin_trap();
        }
        if (written.value().size() != size ||
            std::memcmp(written.value().data(), data, size) != 0) {
            __builtin_trap();
        }
    }
    return 0;
}
