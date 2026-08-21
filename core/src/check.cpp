#include "fauxbuild/check.hpp"

#include <cstdio>
#include <cstdlib>

namespace fauxbuild::detail {

[[noreturn]] void check_failed(const char* expr, const char* file, unsigned line) {
    std::fprintf(stderr, "fauxbuild: check failed: %s (%s:%u)\n", expr, file, line);
    std::abort();
}

} // namespace fauxbuild::detail
