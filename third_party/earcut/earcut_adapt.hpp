#pragma once

// The single place FauxBuild includes earcut, so the library stays an
// implementation detail of derived structural triangulation (D0017): no earcut
// type appears in any FauxBuild header or public API.
//
// Warning suppression is done with pragmas here rather than with -isystem /
// /external:I include flags. Those flags only apply when the header is found
// *through* the system path, and third_party/earcut must also be on the
// ordinary CPPPATH for SCons to scan the dependency at all -- with both
// mechanisms in play, whether the vendored header counts as "external" depends
// on include-path precedence, which differs across toolchains. Pragmas do not
// depend on how the file was located.
//
// The suppressed diagnostics are Mapbox's, not ours: C4458 (declaration hides
// class member) inside earcut, and the narrowing MSVC reports from the
// standard library templates earcut instantiates. Our own code is still built
// with -Wall -Wextra -Werror and /W4 /WX.

#include <array>
#include <cstdint>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif

#include "earcut.hpp"

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace mapbox {
namespace util {

// Point accessors for the double pairs FauxBuild hands earcut. Coordinates
// originate as Build int32 and are exactly representable in a double; the
// conversion happens at the FauxBuild boundary (see exact_as_double), and the
// validation and area-oracle stages either side remain exact integers.
template <>
struct nth<0, std::array<double, 2>> {
    static double get(const std::array<double, 2>& point) { return point[0]; }
};

template <>
struct nth<1, std::array<double, 2>> {
    static double get(const std::array<double, 2>& point) { return point[1]; }
};

} // namespace util
} // namespace mapbox
