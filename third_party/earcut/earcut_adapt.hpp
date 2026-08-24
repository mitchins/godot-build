#pragma once

// Point accessors letting earcut consume exact int64 Build coordinates
// directly. Isolated here so no earcut type appears in any FauxBuild header
// or public API (D0017): the library is an implementation detail of derived
// structural triangulation.

#include <array>
#include <cstdint>

#include "earcut.hpp"

namespace mapbox {
namespace util {

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
