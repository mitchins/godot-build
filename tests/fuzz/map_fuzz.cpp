// libFuzzer-style target: MAP v7 parse + validate + canonical round-trip over
// fully untrusted bytes (task §10). Invariants beyond "no crash":
//  - parse success + validation success -> write -> reparse -> semantic diff
//    must be empty (the writer contract must hold for ANY acceptable world);
//  - parse success implies exact section arithmetic (no trailing data).
// Findings become committed regression inputs (D0010).
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "fauxbuild/map_diff.hpp"
#include "fauxbuild/map_io.hpp"
#include "fauxbuild/map_validate.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view bytes(reinterpret_cast<const char*>(data), size);

    auto parsed = fauxbuild::read_map(bytes, "fuzz");
    if (!parsed.is_ok()) {
        return 0; // malformed input must fail safely, never crash
    }

    const auto report = fauxbuild::validate_map(parsed.value());
    (void)report; // validation itself must complete within bounds on any input

    auto written = fauxbuild::write_map(parsed.value());
    if (!written.is_ok()) {
        __builtin_trap(); // our bug: parsed a map we refuse to serialize
    }
    const std::string_view out(reinterpret_cast<const char*>(written.value().data()),
                               written.value().size());
    auto reparsed = fauxbuild::read_map(out, "fuzz-rewrite");
    if (!reparsed.is_ok()) {
        __builtin_trap(); // our bug: canonical write does not reparse
    }
    if (!fauxbuild::diff_maps(parsed.value(), reparsed.value()).identical) {
        __builtin_trap(); // our bug: canonical round-trip lost semantics
    }
    return 0;
}
