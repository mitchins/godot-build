// Fuzz target: ART container over untrusted bytes. Invariants beyond
// no-crash: parse success -> canonical write -> byte-identical input
// (all blobs are preserved verbatim). Findings become committed regression
// inputs (D0010).
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include "fauxbuild/art.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view bytes(reinterpret_cast<const char*>(data), size);

    auto parsed = fauxbuild::read_art(bytes, "fuzz");
    if (!parsed.is_ok()) {
        return 0; // malformed input must fail safely, never crash
    }
    auto written = fauxbuild::write_art(parsed.value());
    if (!written.is_ok()) {
        __builtin_trap(); // our bug: parsed ART we refuse to write
    }
    if (written.value().size() != size || std::memcmp(written.value().data(), data, size) != 0) {
        __builtin_trap(); // our bug: canonical write is not byte-identical
    }
    return 0;
}
