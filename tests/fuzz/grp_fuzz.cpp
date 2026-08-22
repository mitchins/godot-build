// libFuzzer target: GRP parser over fully untrusted bytes (plan §3.3 fuzz,
// M2). Invariants beyond "no crash": every accepted entry's data range must
// fit the input, and the mounted VFS must serve every entry without
// extraction. Findings become committed regression inputs (D0010).
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "fauxbuild/grp.hpp"
#include "fauxbuild/vfs.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view image(reinterpret_cast<const char*>(data), size);

    fauxbuild::grp::GrpDiagnostics diags;
    auto parsed = fauxbuild::grp::parse(image, "fuzz", &diags);
    if (!parsed.is_ok()) {
        return 0; // malformed input must fail safely, never crash
    }

    // Parser contract: accepted data ranges are in-bounds slices.
    std::uint64_t checksum = 0;
    for (const auto& entry : parsed.value().entries) {
        if (entry.offset + entry.size > size) {
            __builtin_trap(); // our bug: parser accepted an out-of-range entry
        }
        for (std::uint64_t i = 0; i < entry.size; ++i) {
            checksum += data[entry.offset + i];
        }
    }

    // Mount + VFS path must tolerate every accepted image.
    auto mount = fauxbuild::GrpMount::from_image(
        "fuzz", std::vector<std::uint8_t>(data, data + size), &diags);
    if (!mount.is_ok()) {
        __builtin_trap(); // our bug: parse succeeded but mount failed
    }
    fauxbuild::Vfs vfs;
    vfs.add_mount(mount.take());
    for (const auto& key : vfs.keys()) {
        auto file = vfs.open(key);
        if (!file.is_ok()) {
            __builtin_trap(); // our bug: key listed but unreadable
        }
        checksum += file.value().bytes.size();
    }

    (void)checksum;
    return 0;
}
