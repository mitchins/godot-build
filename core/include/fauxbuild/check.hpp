#pragma once

namespace fauxbuild::detail {

[[noreturn]] void check_failed(const char* expr, const char* file, unsigned line);

} // namespace fauxbuild::detail

// Content-safety check. Always enabled in every configuration, independent of
// NDEBUG (plan §3.3, decision D0006). Use for invariants whose violation means
// malformed content or world corruption; never for ordinary error handling.
#define FB_CHECK(cond)                                                                             \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            ::fauxbuild::detail::check_failed(#cond, __FILE__, static_cast<unsigned>(__LINE__));   \
        }                                                                                          \
    } while (0)
