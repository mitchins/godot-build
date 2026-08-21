#include "fauxbuild/version.hpp"

namespace fauxbuild {

const char* build_config() {
#if defined(FAUXBUILD_CONFIG_ASAN)
    return "asan";
#elif defined(FAUXBUILD_CONFIG_RELEASE)
    return "release";
#else
    return "dev";
#endif
}

} // namespace fauxbuild
