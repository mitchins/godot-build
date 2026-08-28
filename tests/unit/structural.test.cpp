#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <limits>

#include <doctest/doctest.h>

#include "fauxbuild/map_io.hpp"
#include "fauxbuild/map_synth.hpp"
#include "fauxbuild/map_v7.hpp"
#include "fauxbuild/result.hpp"
#include "fauxbuild/structural.hpp"

// M5 slice 1: structural geometry derivation. Everything here is pure C++:
// no Godot types, no scene, no renderer (slice 2 owns presentation).

using fauxbuild::build_structural_world;
using fauxbuild::render_z_is_exact;
using fauxbuild::slope_is_evaluable;
using fauxbuild::StructuralOptions;
using fauxbuild::StructuralSurface;
using fauxbuild::StructuralVertex;
using fauxbuild::StructuralWorld;
using fauxbuild::surface_z_at;
using fauxbuild::SurfaceKind;
using fauxbuild::SurfacePlane;
using fauxbuild::to_render_space;
using fauxbuild::synth::map_fixture;

namespace {

constexpr std::int32_t kUnit = 65536; // one Build grid square (matches map_synth)

struct Vec3 {
    double x = 0, y = 0, z = 0;
};

Vec3 operator-(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 as_vec(const StructuralVertex& v) {
    return {v.x, v.y, v.z};
}

Vec3 triangle_normal(const StructuralSurface& surface, std::size_t tri) {
    const auto i0 = surface.indices[tri * 3 + 0];
    const auto i1 = surface.indices[tri * 3 + 1];
    const auto i2 = surface.indices[tri * 3 + 2];
    return cross(as_vec(surface.vertices[i1]) - as_vec(surface.vertices[i0]),
                 as_vec(surface.vertices[i2]) - as_vec(surface.vertices[i0]));
}

const StructuralSurface* find_surface(const StructuralWorld& world, SurfaceKind kind,
                                      std::int16_t sector, std::int16_t wall) {
    for (const auto& surface : world.surfaces) {
        if (surface.kind == kind && surface.sector == sector && surface.wall == wall) {
            return &surface;
        }
    }
    return nullptr;
}

std::size_t count_kind(const StructuralWorld& world, SurfaceKind kind) {
    std::size_t n = 0;
    for (const auto& surface : world.surfaces) {
        n += surface.kind == kind ? 1 : 0;
    }
    return n;
}

// Every triangle references valid vertices and has nonzero area.
void check_triangles_wellformed(const StructuralWorld& world) {
    for (const auto& surface : world.surfaces) {
        REQUIRE(surface.indices.size() % 3 == 0);
        for (const auto index : surface.indices) {
            CHECK(index < surface.vertices.size());
        }
        for (std::size_t t = 0; t < surface.indices.size() / 3; ++t) {
            const Vec3 n = triangle_normal(surface, t);
            CHECK(dot(n, n) > 0.0);
        }
    }
}

// Exact double cross product in the render x-z plane (all fixture coordinates

// are exact in double, so signs are exact too).
double cross2(double ax, double az, double bx, double bz) {
    return ax * bz - az * bx;
}

// Inclusive variant: a point exactly on a triangle edge counts as covered.
// Any valid triangulation of the same region must agree on this, whereas the
// strict test above depends on which diagonals the implementation happens to
// choose. Coverage assertions use this one so a different-but-equally-valid
// triangulation cannot fail them (M5 slice-1 amendment, item F).
bool point_in_triangle_xz_inclusive(const StructuralSurface& surface, std::size_t tri, double px,
                                    double pz) {
    const auto& a = surface.vertices[surface.indices[tri * 3]];
    const auto& b = surface.vertices[surface.indices[tri * 3 + 1]];
    const auto& c = surface.vertices[surface.indices[tri * 3 + 2]];
    const double d1 = cross2(px - a.x, pz - a.z, b.x - a.x, b.z - a.z);
    const double d2 = cross2(px - b.x, pz - b.z, c.x - b.x, c.z - b.z);
    const double d3 = cross2(px - c.x, pz - c.z, a.x - c.x, a.z - c.z);
    const bool neg = d1 < 0 || d2 < 0 || d3 < 0;
    const bool pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}

bool covered_by_floor(const StructuralWorld& world, std::int16_t sector, double px, double pz) {
    for (const auto& surface : world.surfaces) {
        if (surface.kind != SurfaceKind::Floor || surface.sector != sector) {
            continue;
        }
        for (std::size_t t = 0; t < surface.indices.size() / 3; ++t) {
            if (point_in_triangle_xz_inclusive(surface, t, px, pz)) {
                return true;
            }
        }
    }
    return false;
}

StructuralWorld build_fixture(const std::string& name) {
    auto map = map_fixture(name);
    REQUIRE(map.is_ok());
    auto world = build_structural_world(map.value());
    REQUIRE(world.is_ok());
    return world.take();
}

} // namespace

// ---------------------------------------------------------------------------
// A. Coordinate conversion
// ---------------------------------------------------------------------------

TEST_CASE("coordinate conversion is exact, reversible, and centralized") {
    // Build (X, Y, Z) -> render (X, -Z/16, Y) with the default 2^-11
    // horizontal scale: one grid square 65536 -> 32 render units. Build Z is
    // 16x the horizontal unit scale (D0016 amendment), so the vertical
    // divisor is 2048 * 16 = 32768.
    const StructuralVertex v = to_render_space(kUnit, -2 * kUnit, 4096);
    CHECK(v.x == 32.0);
    CHECK(v.y == -0.125); // 4096 / 32768, not 4096 / 2048
    CHECK(v.z == -64.0);

    // THE metric property (D0016 amendment): 1024 horizontal units and 16384
    // vertical units are the SAME physical distance and must produce equal
    // render lengths. This is the invariant that isotropic scaling breaks.
    const StructuralVertex horizontal = to_render_space(1024, 1024, 0);
    const StructuralVertex vertical = to_render_space(0, 0, 16384);
    CHECK(horizontal.x == 0.5);
    CHECK(horizontal.z == 0.5);
    CHECK(vertical.y == -0.5);
    CHECK(horizontal.x == -vertical.y);
    CHECK(horizontal.z == -vertical.y);

    // Exact round trip for adversarial int32 values. Each axis reverses with
    // its own inverse; both are powers of two, so both are exact.
    const std::int32_t xs[] = {-2147483647 - 1, 2147483647, 0, -1, 65535};
    for (const std::int32_t x : xs) {
        for (const std::int32_t z : xs) {
            const StructuralVertex r = to_render_space(x, x, z);
            CHECK(static_cast<std::int64_t>(r.x * 2048.0) == static_cast<std::int64_t>(x));
            CHECK(static_cast<std::int64_t>(r.z * 2048.0) == static_cast<std::int64_t>(x));
            CHECK(static_cast<std::int64_t>(r.y * 32768.0) ==
                  static_cast<std::int64_t>(-static_cast<std::int64_t>(z)));
        }
    }

    // Non-power-of-two scales are rejected with a structured error.
    auto map = map_fixture("square_room");
    REQUIRE(map.is_ok());
    StructuralOptions bad;
    bad.scale = 1.0 / 1000.0;
    auto world = build_structural_world(map.value(), bad);
    REQUIRE_FALSE(world.is_ok());
    CHECK(world.error().code == fauxbuild::ErrorCode::Unsupported);

    // ...and so is a horizontal scale that is itself a conforming power of
    // two but whose DERIVED vertical scale is not. 2^-1020 / 16 is subnormal,
    // so exact reversibility is lost. This must be a structured error, not an
    // FB_CHECK abort: it is a caller's option value, not our own bug.
    // ldexp(1, -1020) is a power of two by construction, so the horizontal
    // scale itself conforms; only the derived vertical one does not.
    StructuralOptions too_small;
    too_small.scale = std::ldexp(1.0, -1020);
    auto shrunk = build_structural_world(map.value(), too_small);
    REQUIRE_FALSE(shrunk.is_ok());
    CHECK(shrunk.error().code == fauxbuild::ErrorCode::Unsupported);

    // The default scale is comfortably inside the usable range.
    StructuralOptions ok_options;
    auto fine = build_structural_world(map.value(), ok_options);
    CHECK(fine.is_ok());
}

TEST_CASE("metric_cube: 1024 horizontal and 16384 vertical are equal render lengths") {
    // The regression pin for the D0016 amendment. This fixture authors a room
    // 1024 Build units across and 16384 Build units tall -- the same physical
    // distance under the format's 16:1 vertical unit ratio -- so its derived
    // shell must measure equal on all three render axes. Isotropic scaling
    // makes it 16x too tall, which is the defect this exists to catch.
    const StructuralWorld world = build_fixture("metric_cube");
    check_triangles_wellformed(world);
    REQUIRE_FALSE(world.surfaces.empty());

    double min_x = 0.0, max_x = 0.0, min_y = 0.0, max_y = 0.0, min_z = 0.0, max_z = 0.0;
    bool first = true;
    for (const auto& surface : world.surfaces) {
        for (const auto& v : surface.vertices) {
            if (first) {
                min_x = max_x = v.x;
                min_y = max_y = v.y;
                min_z = max_z = v.z;
                first = false;
                continue;
            }
            min_x = std::min(min_x, v.x);
            max_x = std::max(max_x, v.x);
            min_y = std::min(min_y, v.y);
            max_y = std::max(max_y, v.y);
            min_z = std::min(min_z, v.z);
            max_z = std::max(max_z, v.z);
        }
    }
    const double extent_x = max_x - min_x;
    const double extent_y = max_y - min_y;
    const double extent_z = max_z - min_z;

    // Spec-derived, not implementation-derived: 1024 * 2^-11 = 0.5.
    CHECK(extent_x == 0.5);
    CHECK(extent_z == 0.5);
    CHECK(extent_y == 0.5); // 16384 / (2048 * 16)
    CHECK(extent_y == extent_x);
    CHECK(extent_y == extent_z);
}

// ---------------------------------------------------------------------------
// B. Square room
// ---------------------------------------------------------------------------

TEST_CASE("square room: floor, ceiling, four solid walls, facing inwards") {
    const StructuralWorld world = build_fixture("square_room");
    check_triangles_wellformed(world);

    // Canonical order: floor, ceiling, then walls ascending.
    REQUIRE(world.surfaces.size() == 6);
    CHECK(world.surfaces[0].kind == SurfaceKind::Floor);
    CHECK(world.surfaces[1].kind == SurfaceKind::Ceiling);
    for (std::int16_t w = 0; w < 4; ++w) {
        CHECK(world.surfaces[2 + static_cast<std::size_t>(w)].kind == SurfaceKind::SolidWall);
        CHECK(world.surfaces[2 + static_cast<std::size_t>(w)].wall == w);
        CHECK(world.surfaces[2 + static_cast<std::size_t>(w)].sector == 0);
    }

    const StructuralSurface& floor = world.surfaces[0];
    CHECK(floor.wall == -1);
    CHECK(floor.appearance.picnum == 101); // inert source metadata preserved
    REQUIRE(floor.indices.size() == 6);
    CHECK(floor.vertices.size() == 4);

    // Fixture planes: floorz=0, ceilingz=16384 (this M3 fixture is inverted:
    // its ceiling plane sits below its floor plane in render space). Floors
    // must face the ceiling plane and ceilings the floor plane, opposite to
    // each other, so back-face culling works for either convention.
    const double floor_y = -0.0;
    const double ceiling_y = -16384.0 / 32768.0; // vertical scale, D0016 amendment
    for (const auto& vertex : floor.vertices) {
        CHECK(vertex.y == floor_y);
    }
    for (const auto& vertex : world.surfaces[1].vertices) {
        CHECK(vertex.y == ceiling_y);
    }
    for (std::size_t t = 0; t < 2; ++t) {
        const Vec3 fn = triangle_normal(floor, t);
        const Vec3 cn = triangle_normal(world.surfaces[1], t);
        CHECK(dot(fn, cn) < 0.0);                  // opposite winding
        CHECK(fn.y * (ceiling_y - floor_y) > 0.0); // floor faces the ceiling plane
        CHECK(cn.y * (floor_y - ceiling_y) > 0.0); // ceiling faces the floor plane
    }

    // Wall normals point into the sector (toward the room centre).
    const Vec3 centre{16.0, (floor_y + ceiling_y) / 2.0, 16.0};
    for (std::size_t i = 2; i < world.surfaces.size(); ++i) {
        const StructuralSurface& wall = world.surfaces[i];
        for (std::size_t t = 0; t < 2; ++t) {
            const Vec3 n = triangle_normal(wall, t);
            const Vec3 to_centre = centre - as_vec(wall.vertices[wall.indices[t * 3]]);
            CHECK(dot(n, to_centre) > 0.0);
        }
    }
}

// ---------------------------------------------------------------------------
// C. Portal walls
// ---------------------------------------------------------------------------

TEST_CASE("asymmetric_probe: render-space vertices are component-distinct") {
    // The M5 slice-2 boundary probe fixture: every surface vertex must have
    // three pairwise-distinct components so an accidental axis swap or sign
    // flip at the Godot boundary cannot survive component-for-component
    // comparison. If someone "fixes" the fixture to a symmetric shape, this
    // case fails here rather than silently weakening the scene gate.
    const StructuralWorld world = build_fixture("asymmetric_probe");
    REQUIRE(world.surfaces.size() == 6);
    for (const auto& surface : world.surfaces) {
        for (const auto& vertex : surface.vertices) {
            CHECK(vertex.x != vertex.y);
            CHECK(vertex.x != vertex.z);
            CHECK(vertex.y != vertex.z);
        }
    }
    // Floor/ceiling planes at the authored heights under (x, -z/16, y) with
    // the 2^-11 horizontal scale: the vertical divisor is 2048 * 16 = 32768.
    for (const auto& vertex : world.surfaces[0].vertices) {
        CHECK(vertex.y == -9000.0 / 32768.0);
    }
    for (const auto& vertex : world.surfaces[1].vertices) {
        CHECK(vertex.y == -3000.0 / 32768.0);
    }
}

TEST_CASE("two_sector_portal: the opening is not closed by any wall") {
    const StructuralWorld world = build_fixture("two_sector_portal");
    check_triangles_wellformed(world);

    // Equal heights: no portal spans at all; the shared walls 1 and 7 emit
    // nothing; the other six stay solid.
    CHECK(count_kind(world, SurfaceKind::PortalUpper) == 0);
    CHECK(count_kind(world, SurfaceKind::PortalLower) == 0);
    CHECK(count_kind(world, SurfaceKind::SolidWall) == 6);
    CHECK(count_kind(world, SurfaceKind::Floor) == 2);
    CHECK(count_kind(world, SurfaceKind::Ceiling) == 2);
    for (std::int16_t wall = 1; wall <= 7; wall += 6) {
        for (const SurfaceKind kind :
             {SurfaceKind::SolidWall, SurfaceKind::PortalUpper, SurfaceKind::PortalLower}) {
            CHECK(find_surface(world, kind, 0, wall) == nullptr);
            CHECK(find_surface(world, kind, 1, wall) == nullptr);
        }
    }
}

TEST_CASE("portal_heights: one upper and one lower span outside the opening") {
    const StructuralWorld world = build_fixture("portal_heights");
    check_triangles_wellformed(world);

    // A spans Build Z [0, 16384]; B's window [4096, 8192] is strictly inside,
    // so A's side of the shared wall (wall 1) shows upper + lower spans and
    // B's side (wall 7) shows nothing.
    CHECK(count_kind(world, SurfaceKind::PortalUpper) == 1);
    CHECK(count_kind(world, SurfaceKind::PortalLower) == 1);
    const StructuralSurface* upper = find_surface(world, SurfaceKind::PortalUpper, 0, 1);
    const StructuralSurface* lower = find_surface(world, SurfaceKind::PortalLower, 0, 1);
    REQUIRE(upper != nullptr);
    REQUIRE(lower != nullptr);
    CHECK(find_surface(world, SurfaceKind::SolidWall, 0, 1) == nullptr);
    CHECK(find_surface(world, SurfaceKind::SolidWall, 1, 7) == nullptr);
    CHECK(find_surface(world, SurfaceKind::PortalUpper, 1, 7) == nullptr);
    CHECK(find_surface(world, SurfaceKind::PortalLower, 1, 7) == nullptr);

    // Upper span covers render Y [0, -2] (Build Z 0..4096); lower covers
    // [-4, -8] (Build Z 8192..16384).
    auto y_extent = [](const StructuralSurface& s) {
        double lo = s.vertices[0].y, hi = s.vertices[0].y;
        for (const auto& v : s.vertices) {
            lo = v.y < lo ? v.y : lo;
            hi = v.y > hi ? v.y : hi;
        }
        return std::pair<double, double>{lo, hi};
    };
    // Authored Build Z heights over the vertical scale 2048 * 16 = 32768.
    const auto [ulo, uhi] = y_extent(*upper);
    CHECK(ulo == -4096.0 / 32768.0);
    CHECK(uhi == 0.0);
    const auto [llo, lhi] = y_extent(*lower);
    CHECK(llo == -16384.0 / 32768.0);
    CHECK(lhi == -8192.0 / 32768.0);

    // Portal order per wall: upper before lower.
    std::size_t upper_at = 0, lower_at = 0;
    for (std::size_t i = 0; i < world.surfaces.size(); ++i) {
        if (&world.surfaces[i] == upper) {
            upper_at = i;
        }
        if (&world.surfaces[i] == lower) {
            lower_at = i;
        }
    }
    CHECK(upper_at < lower_at);
}

TEST_CASE("portal_step_floor: a raised neighbour floor makes exactly one lower span") {
    const StructuralWorld world = build_fixture("portal_step_floor");
    check_triangles_wellformed(world);
    CHECK(count_kind(world, SurfaceKind::PortalUpper) == 0);
    CHECK(count_kind(world, SurfaceKind::PortalLower) == 1);
    CHECK(find_surface(world, SurfaceKind::PortalLower, 0, 1) != nullptr);
    CHECK(find_surface(world, SurfaceKind::PortalLower, 1, 7) == nullptr);
}

// ---------------------------------------------------------------------------
// D. Non-convex sector
// ---------------------------------------------------------------------------

TEST_CASE("non_convex: triangulated without filling the concavity") {
    const StructuralWorld world = build_fixture("non_convex");
    check_triangles_wellformed(world);

    const StructuralSurface* floor = find_surface(world, SurfaceKind::Floor, 0, -1);
    REQUIRE(floor != nullptr);
    CHECK(floor->indices.size() == 12); // hexagon -> four triangles

    // Total floor area equals the L-shape area: the 2u x 2u square minus the
    // u x u notch = 3 u^2, in render units.
    const double unit = 1.0 * kUnit / 2048.0;
    const double expected = 3.0 * unit * unit;
    double area = 0;
    for (std::size_t t = 0; t < floor->indices.size() / 3; ++t) {
        const auto i0 = floor->indices[t * 3 + 0];
        const auto i1 = floor->indices[t * 3 + 1];
        const auto i2 = floor->indices[t * 3 + 2];
        const double d1 = cross2(floor->vertices[i1].x - floor->vertices[i0].x,
                                 floor->vertices[i1].z - floor->vertices[i0].z,
                                 floor->vertices[i2].x - floor->vertices[i0].x,
                                 floor->vertices[i2].z - floor->vertices[i0].z);
        area += std::abs(d1) / 2.0;
    }
    CHECK(area == doctest::Approx(expected).epsilon(1e-12));

    // The notch (missing top-right quadrant) stays empty; the bottom arm is
    // covered. Edge-inclusive, so diagonal choice cannot decide it.
    CHECK_FALSE(covered_by_floor(world, 0, 1.5 * unit, 1.5 * unit));
    CHECK(covered_by_floor(world, 0, 0.5 * unit, 0.125 * unit));
}

// ---------------------------------------------------------------------------
// E. Multi-loop sectors: holes stay holes
// ---------------------------------------------------------------------------

TEST_CASE("multi_loop: the hole stays empty and walls bound it") {
    const StructuralWorld world = build_fixture("multi_loop");
    check_triangles_wellformed(world);

    const StructuralSurface* floor = find_surface(world, SurfaceKind::Floor, 0, -1);
    REQUIRE(floor != nullptr);

    // Area = outer minus hole.
    const double unit = 1.0 * kUnit / 2048.0;
    const double expected = unit * unit - (unit / 2.0) * (unit / 2.0);
    double area = 0;
    for (std::size_t t = 0; t < floor->indices.size() / 3; ++t) {
        const auto i0 = floor->indices[t * 3 + 0];
        const auto i1 = floor->indices[t * 3 + 1];
        const auto i2 = floor->indices[t * 3 + 2];
        const double d1 = cross2(floor->vertices[i1].x - floor->vertices[i0].x,
                                 floor->vertices[i1].z - floor->vertices[i0].z,
                                 floor->vertices[i2].x - floor->vertices[i0].x,
                                 floor->vertices[i2].z - floor->vertices[i0].z);
        area += std::abs(d1) / 2.0;
    }
    CHECK(area == doctest::Approx(expected).epsilon(1e-12));

    // Hole interior uncovered; material covered. Coverage is edge-inclusive, so
    // this holds for any valid triangulation rather than one diagonal choice.
    const double c = 0.5 * unit;
    CHECK_FALSE(covered_by_floor(world, 0, c, c));
    CHECK(covered_by_floor(world, 0, 0.125 * unit, 0.375 * unit));

    // All eight walls are solid spans; the hole walls (4..7) face away from
    // the hole centre (toward the material).
    CHECK(count_kind(world, SurfaceKind::SolidWall) == 8);
    const Vec3 hole_centre{c, 0.0, c};
    for (std::int16_t w = 4; w <= 7; ++w) {
        const StructuralSurface* wall = find_surface(world, SurfaceKind::SolidWall, 0, w);
        REQUIRE(wall != nullptr);
        for (std::size_t t = 0; t < 2; ++t) {
            const Vec3 n = triangle_normal(*wall, t);
            const Vec3 to_hole = hole_centre - as_vec(wall->vertices[wall->indices[t * 3]]);
            CHECK(dot(n, to_hole) < 0.0);
        }
    }
}

TEST_CASE("double_hole: two holes are bridged and both stay empty") {
    const StructuralWorld world = build_fixture("double_hole");
    check_triangles_wellformed(world);

    const StructuralSurface* floor = find_surface(world, SurfaceKind::Floor, 0, -1);
    REQUIRE(floor != nullptr);
    const double unit = 1.0 * kUnit / 2048.0;
    const double expected = (2 * unit) * (2 * unit) - 2 * (unit / 2.0) * (unit / 2.0);
    double area = 0;
    for (std::size_t t = 0; t < floor->indices.size() / 3; ++t) {
        const auto i0 = floor->indices[t * 3 + 0];
        const auto i1 = floor->indices[t * 3 + 1];
        const auto i2 = floor->indices[t * 3 + 2];
        const double d1 = cross2(floor->vertices[i1].x - floor->vertices[i0].x,
                                 floor->vertices[i1].z - floor->vertices[i0].z,
                                 floor->vertices[i2].x - floor->vertices[i0].x,
                                 floor->vertices[i2].z - floor->vertices[i0].z);
        area += std::abs(d1) / 2.0;
    }
    CHECK(area == doctest::Approx(expected).epsilon(1e-12));

    CHECK_FALSE(covered_by_floor(world, 0, 0.5 * unit, 0.5 * unit));
    CHECK_FALSE(covered_by_floor(world, 0, 1.5 * unit, 1.5 * unit));
    CHECK(covered_by_floor(world, 0, 1.0 * unit, 0.25 * unit));
    CHECK(count_kind(world, SurfaceKind::SolidWall) == 12);
}

// ---------------------------------------------------------------------------
// F. Determinism
// ---------------------------------------------------------------------------

TEST_CASE("structural worlds are identical across independent rebuilds") {
    for (const auto& name : fauxbuild::synth::map_fixture_names()) {
        auto map = map_fixture(name);
        REQUIRE(map.is_ok());
        auto a = build_structural_world(map.value());
        auto b = build_structural_world(map.value());
        REQUIRE(a.is_ok());
        REQUIRE(b.is_ok());
        CHECK(a.value() == b.value());

        // Rebuild through a serialize/parse round trip: same world.
        auto bytes = fauxbuild::write_map(map.value());
        REQUIRE(bytes.is_ok());
        auto reparsed = fauxbuild::read_map(
            std::string_view(reinterpret_cast<const char*>(bytes.value().data()),
                             bytes.value().size()),
            name);
        REQUIRE(reparsed.is_ok());
        auto c = build_structural_world(reparsed.value());
        REQUIRE(c.is_ok());
        CHECK(a.value() == c.value());
    }
}

// ---------------------------------------------------------------------------
// G. Malformed / unrepresentable content
// ---------------------------------------------------------------------------

namespace {

fauxbuild::mapv7::MapData hand_built_map() {
    // Square room, walls 0..3, sector 0. Tests mutate copies of this.
    fauxbuild::mapv7::MapData map;
    using fauxbuild::synth::make_sector;
    using fauxbuild::synth::make_wall;
    map.walls.push_back(make_wall(0, 0, 1));
    map.walls.push_back(make_wall(kUnit, 0, 2));
    map.walls.push_back(make_wall(kUnit, kUnit, 3));
    map.walls.push_back(make_wall(0, kUnit, 0));
    map.sectors.push_back(make_sector(0, 4));
    map.start = {kUnit / 2, kUnit / 2, 4096, 512, 0};
    return map;
}

} // namespace

TEST_CASE("unvalidated topology surfaces the validator's structured error") {
    auto map = hand_built_map();
    map.walls[1].nextsector = 0; // nextsector without nextwall
    auto world = build_structural_world(map);
    REQUIRE_FALSE(world.is_ok());
    CHECK(world.error().record == std::string("wall[1]"));
    CHECK(world.error().code == fauxbuild::ErrorCode::InvalidNextSector);
    CHECK(world.error().detail.find("map failed structural validation") != std::string::npos);
}

TEST_CASE("degenerate loop geometry fails closed at triangulation") {
    // A two-wall loop (A -> B -> A) validates clean but has zero area.
    auto map = hand_built_map();
    map.walls.clear();
    map.sectors.clear();
    using fauxbuild::synth::make_sector;
    using fauxbuild::synth::make_wall;
    map.walls.push_back(make_wall(0, 0, 1));
    map.walls.push_back(make_wall(kUnit, 0, 0));
    map.sectors.push_back(make_sector(0, 2));
    map.start = {0, 0, 4096, 512, 0};
    auto world = build_structural_world(map);
    REQUIRE_FALSE(world.is_ok());
    CHECK(world.error().record == std::string("sector[0]"));
    CHECK(world.error().detail.find("triangulation failed") != std::string::npos);
}

TEST_CASE("self-intersecting bowtie loop fails closed") {
    auto map = hand_built_map();
    map.walls[0] = fauxbuild::synth::make_wall(0, 0, 1);
    map.walls[1] = fauxbuild::synth::make_wall(2 * kUnit, 2 * kUnit, 2);
    map.walls[2] = fauxbuild::synth::make_wall(2 * kUnit, 0, 3);
    map.walls[3] = fauxbuild::synth::make_wall(0, 2 * kUnit, 0);
    auto world = build_structural_world(map);
    REQUIRE_FALSE(world.is_ok());
    CHECK(world.error().record == std::string("sector[0]"));
    CHECK(world.error().detail.find("triangulation failed") != std::string::npos);
}

TEST_CASE("a hole loop outside the outer loop fails closed") {
    auto map = hand_built_map();
    using fauxbuild::synth::make_wall;
    // Second loop far away from the square: closed, owned, but not inside.
    const std::int32_t hx[] = {4 * kUnit, 5 * kUnit, 5 * kUnit, 4 * kUnit};
    const std::int32_t hy[] = {4 * kUnit, 4 * kUnit, 5 * kUnit, 5 * kUnit};
    for (std::size_t i = 0; i < 4; ++i) {
        map.walls.push_back(make_wall(hx[i], hy[i], static_cast<std::int16_t>(4 + (i + 1) % 4)));
    }
    map.sectors[0].wallnum = 8;
    auto world = build_structural_world(map);
    REQUIRE_FALSE(world.is_ok());
    CHECK(world.error().record == std::string("sector[0]"));
    // The rule is "outside the outer loop", not "not strictly inside": real
    // Build sectors legitimately share and touch their own hole boundaries,
    // and pinning the old message would re-impose the over-strict rule the
    // amendment removed (D0017).
    CHECK(world.error().detail.find("outside the outer loop") != std::string::npos);
}

// ---------------------------------------------------------------------------
// H. Slope-marked sectors are diagnosed, not interpreted
// ---------------------------------------------------------------------------

TEST_CASE("slope evaluator: hinge invariance, 45-degree ramp, sign, and flag") {
    // The ramp fixture is a right triangle A=(0,0) B=(1024,0) C=(0,1024) with
    // the first wall A->B as the hinge, floorz 32768. Every expected number
    // below comes from the published definition (heinum 4096 = 45 degrees)
    // and the ratified 16:1 metric, NOT from the implementation.
    auto map = map_fixture("ramp_floor_pos");
    REQUIRE(map.is_ok());
    const auto& source = map.value();
    const std::int64_t base = 32768;

    // A. Hinge invariance: both hinge endpoints hold base Z.
    CHECK(surface_z_at(source, 0, SurfacePlane::Floor, 0, 0) == base);
    CHECK(surface_z_at(source, 0, SurfacePlane::Floor, 1024, 0) == base);
    // ...and so does every other point ON the hinge line, including beyond it.
    CHECK(surface_z_at(source, 0, SurfacePlane::Floor, 512, 0) == base);
    CHECK(surface_z_at(source, 0, SurfacePlane::Floor, -4096, 0) == base);

    // B. 45 degrees: 1024 units perpendicular must give exactly
    //    1024 * 4096 / 256 = 16384 Build Z, i.e. equal physical rise and run.
    CHECK(surface_z_at(source, 0, SurfacePlane::Floor, 0, 1024) == base + 16384);
    // Linear in the perpendicular distance.
    CHECK(surface_z_at(source, 0, SurfacePlane::Floor, 0, 512) == base + 8192);
    CHECK(surface_z_at(source, 0, SurfacePlane::Floor, 700, 256) == base + 4096);
    // The sign follows cross((B-A), (P-A)): the far side is the other way.
    CHECK(surface_z_at(source, 0, SurfacePlane::Floor, 0, -1024) == base - 16384);

    // Equal physical rise and run, stated at the render boundary.
    const StructuralVertex hinge = to_render_space(0, 0, static_cast<std::int32_t>(base));
    const StructuralVertex apex = to_render_space(
        0, 1024, static_cast<std::int32_t>(surface_z_at(source, 0, SurfacePlane::Floor, 0, 1024)));
    CHECK(std::fabs(apex.y - hinge.y) == std::fabs(apex.z - hinge.z));

    // C. Negative heinum: equal magnitude, opposite delta, exactly.
    auto negative = map_fixture("ramp_floor_neg");
    REQUIRE(negative.is_ok());
    for (const std::int32_t probe : {0, 128, 512, 1024}) {
        const std::int64_t up = surface_z_at(source, 0, SurfacePlane::Floor, 0, probe) - base;
        const std::int64_t down =
            surface_z_at(negative.value(), 0, SurfacePlane::Floor, 0, probe) - base;
        CHECK(up == -down);
    }

    // D. Flag clear: the same nonzero heinum is an ignored leftover.
    auto stale = map_fixture("ramp_stale_heinum");
    REQUIRE(stale.is_ok());
    CHECK(stale.value().sectors[0].floorheinum == 4096); // still set in the record
    CHECK((stale.value().sectors[0].floorstat & fauxbuild::mapv7::kStatSloped) == 0);
    for (const std::int32_t probe : {0, 512, 1024}) {
        CHECK(surface_z_at(stale.value(), 0, SurfacePlane::Floor, 0, probe) == base);
    }
    const StructuralWorld flat = build_fixture("ramp_stale_heinum");
    const StructuralSurface* flat_floor = find_surface(flat, SurfaceKind::Floor, 0, -1);
    REQUIRE(flat_floor != nullptr);
    for (const auto& v : flat_floor->vertices) {
        CHECK(v.y == -32768.0 / 32768.0);
    }

    // The ceiling plane uses the same evaluator and the same convention.
    auto ceiling_map = map_fixture("ramp_ceiling");
    REQUIRE(ceiling_map.is_ok());
    const std::int64_t ceiling_base = -32768;
    CHECK(surface_z_at(ceiling_map.value(), 0, SurfacePlane::Ceiling, 0, 0) == ceiling_base);
    CHECK(surface_z_at(ceiling_map.value(), 0, SurfacePlane::Ceiling, 1024, 0) == ceiling_base);
    CHECK(surface_z_at(ceiling_map.value(), 0, SurfacePlane::Ceiling, 0, 1024) ==
          ceiling_base + 16384);
    // The floor of that same fixture is unflagged and stays flat.
    CHECK(surface_z_at(ceiling_map.value(), 0, SurfacePlane::Floor, 0, 1024) == 32768);
}

TEST_CASE("slope evaluator: deterministic corpus with independently derived oracles") {
    // The expected values are NOT read back from this implementation: they
    // were produced in Python, a different language and runtime, by applying
    // the documented recipe (exact integer cross product and squared length,
    // one binary64 division and sqrt, symmetric rounding). Agreement means the
    // C++ matches the specification, not merely itself.
    //
    // These same integers must hold in dev, asan and release here, and on
    // Linux x86_64, macOS arm64 and Windows MSVC in CI. No platform tolerance
    // is allowed: a mismatch is a real divergence to investigate, not noise.
    struct SlopeCase {
        const char* label;
        std::int32_t ax, ay, bx, by; // hinge A -> B
        std::int32_t px, py;         // query point
        std::int16_t heinum;
        std::int64_t expected_delta; // from base Z 0
    };
    static const SlopeCase kCases[] = {
        {"axis_x_pos", 0, 0, 1024, 0, 0, 1024, 4096, 16384},
        {"axis_x_neg_side", 0, 0, 1024, 0, 0, -1024, 4096, -16384},
        {"axis_x_neg_heinum", 0, 0, 1024, 0, 0, 1024, -4096, -16384},
        {"axis_y", 0, 0, 0, 1024, 1024, 0, 4096, -16384},
        {"pyth_345", 0, 0, 3000, 4000, -4000, 3000, 4096, 80000},
        {"pyth_345_far", 0, 0, 3000, 4000, 8000, -6000, 2048, -80000},
        {"irrational_diag", 0, 0, 1000, 1000, -1000, 1000, 4096, 22627},
        {"irrational_diag_n", 0, 0, 1000, 1000, 1000, -1000, 4096, -22627},
        {"exact_half_pos", 0, 0, 1024, 0, 0, 1, 1664, 7},
        {"exact_half_neg", 0, 0, 1024, 0, 0, 1, -1664, -7},
        {"just_below_half", 0, 0, 1024, 0, 0, 1, 1663, 6},
        {"just_above_half", 0, 0, 1024, 0, 0, 1, 1665, 7},
        {"tiny_hinge", 0, 0, 1, 0, 0, 7, 4096, 112},
        {"on_hinge", 0, 0, 1024, 0, 512, 0, 4096, 0},
        // Extreme coordinates: a hinge spanning the whole int32 range. The
        // EXACT result here is 1048577.5, a true half-tie, but the cross
        // product is 9007212137545725 -- just past 2^53 -- so binary64 sees
        // 1048577.4999999998 and rounds to 1048577. Pinned to the behaviour
        // that actually occurs, with the limit stated rather than hidden:
        // beyond 2^53 the result is accurate to one Build Z unit, and the
        // symmetry check below still holds exactly. Unreachable from real
        // content, where cross products sit around 2^40.
        {"int32_span_half_tie", -2147483647 - 1, 0, 2147483647, 0, 0, 2097155, 128, 1048577},
    };

    for (const SlopeCase& c : kCases) {
        // A bare three-wall sector whose FIRST wall is the hinge under test.
        fauxbuild::mapv7::MapData map;
        fauxbuild::mapv7::Wall a;
        a.x = c.ax;
        a.y = c.ay;
        a.point2 = 1;
        fauxbuild::mapv7::Wall b;
        b.x = c.bx;
        b.y = c.by;
        b.point2 = 2;
        fauxbuild::mapv7::Wall third;
        third.x = c.ax;
        third.y = c.by + 1;
        third.point2 = 0;
        map.walls = {a, b, third};
        fauxbuild::mapv7::Sector sector;
        sector.wallptr = 0;
        sector.wallnum = 3;
        sector.floorz = 0;
        sector.floorstat = fauxbuild::mapv7::kStatSloped;
        sector.floorheinum = c.heinum;
        map.sectors = {sector};

        const std::int64_t got = surface_z_at(map, 0, SurfacePlane::Floor, c.px, c.py);
        INFO("case: " << c.label);
        CHECK(got == c.expected_delta);

        // Symmetry: negating the heinum negates the result exactly. This is
        // what the symmetric rounding policy buys, and it must hold even at a
        // half boundary.
        map.sectors[0].floorheinum = static_cast<std::int16_t>(-c.heinum);
        const std::int64_t mirrored = surface_z_at(map, 0, SurfacePlane::Floor, c.px, c.py);
        CHECK(mirrored == -c.expected_delta);
    }
}

TEST_CASE("derived slope Z wider than int32 reaches render space unnarrowed") {
    // The MAP stores base Z as int32, but an evaluated slope Z is derived and
    // is not bounded by it. The apex of this fixture is 200,000,000 units from
    // the hinge, so at heinum 4096 the delta is 200000000 * 16 =
    // 3,200,000,000 -- outside int32 in both directions.
    constexpr std::int64_t kApexDelta = 3200000000LL;
    static_assert(kApexDelta > INT32_MAX, "the fixture must actually leave int32");

    auto positive = map_fixture("slope_wide_z_pos");
    REQUIRE(positive.is_ok());
    const std::int64_t up = surface_z_at(positive.value(), 0, SurfacePlane::Floor, 0, 200000000);
    CHECK(up == kApexDelta);
    CHECK(up > INT32_MAX);

    auto negative = map_fixture("slope_wide_z_neg");
    REQUIRE(negative.is_ok());
    const std::int64_t down = surface_z_at(negative.value(), 0, SurfacePlane::Floor, 0, 200000000);
    CHECK(down == -kApexDelta);
    CHECK(down < INT32_MIN);

    // The render conversion carries the wide value: 3.2e9 / 32768 = 97656.25,
    // exactly representable. A silent int32 narrowing would wrap to a
    // completely different height.
    CHECK(render_z_is_exact(kApexDelta));
    const StructuralVertex wide = to_render_space(0, 200000000, kApexDelta);
    CHECK(wide.y == -3200000000.0 / 32768.0);
    CHECK(wide.y == -97656.25);
    const StructuralVertex narrowed =
        to_render_space(0, 200000000, static_cast<std::int32_t>(kApexDelta));
    CHECK(narrowed.y != wide.y); // the old path really did lose the value

    // ...and the emitted surface carries it, not just the evaluator.
    const StructuralWorld world = build_fixture("slope_wide_z_pos");
    CHECK(world.diagnostics.empty());
    const StructuralSurface* floor = find_surface(world, SurfaceKind::Floor, 0, -1);
    REQUIRE(floor != nullptr);
    double lowest = 0.0;
    for (const auto& v : floor->vertices) {
        lowest = std::min(lowest, v.y);
    }
    CHECK(lowest == -97656.25);
}

TEST_CASE("slope-collapsed wall spans become wedges, not zero-area quads") {
    // Evaluated slope heights can close a span at one endpoint. The derived
    // shape is then a triangular WEDGE. Emitting a quad anyway staples a
    // zero-area triangle onto it; discarding the span entirely throws away
    // real geometry. Both are wrong, in opposite directions.
    //
    //   open at A and B       -> quad, two triangles
    //   closed at exactly one -> wedge, one triangle
    //   closed at both        -> omitted
    //
    // The fixture's BASE intervals are all open, so every case below is
    // produced by slope evaluation and not by the flat decision.
    auto map = map_fixture("portal_slope_collapse");
    REQUIRE(map.is_ok());
    const StructuralWorld world = build_fixture("portal_slope_collapse");

    // The zero-area gate: every emitted triangle, in every surface.
    check_triangles_wellformed(world);

    auto span_for = [&](SurfaceKind kind, std::int16_t wall) -> const StructuralSurface* {
        for (const auto& surface : world.surfaces) {
            if (surface.kind == kind && surface.sector == 0 && surface.wall == wall) {
                return &surface;
            }
        }
        return nullptr;
    };
    auto plane_z = [&](SurfacePlane plane, std::int16_t sector, std::int32_t x, std::int32_t y) {
        return surface_z_at(map.value(), sector, plane, x, y);
    };

    // Wall 0 is solid and its span is open at both ends: an ordinary quad.
    const StructuralSurface* solid = span_for(SurfaceKind::SolidWall, 0);
    REQUIRE(solid != nullptr);
    CHECK(solid->vertices.size() == 4);
    CHECK(solid->indices.size() == 6);

    // Walls 1 and 3 run perpendicular to the hinge from opposite sides, so
    // their spans close at opposite endpoints. Between them they cover both.
    int closed_at_a = 0;
    int closed_at_b = 0;
    for (const std::int16_t wall : {std::int16_t{1}, std::int16_t{3}}) {
        const auto& near_wall = map.value().walls[static_cast<std::size_t>(wall)];
        const auto& far_wall = map.value().walls[static_cast<std::size_t>(near_wall.point2)];
        const std::int16_t neighbour = near_wall.nextsector;
        REQUIRE(neighbour >= 0);

        for (const SurfaceKind kind : {SurfaceKind::PortalUpper, SurfaceKind::PortalLower}) {
            const StructuralSurface* span = span_for(kind, wall);
            REQUIRE(span != nullptr);
            // A wedge: three vertices, exactly one triangle.
            CHECK(span->vertices.size() == 3);
            CHECK(span->indices.size() == 3);

            // Which endpoint closed, derived from the evaluator rather than
            // read off the emitted vertices.
            const SurfacePlane plane =
                kind == SurfaceKind::PortalUpper ? SurfacePlane::Ceiling : SurfacePlane::Floor;
            const std::int64_t own_a = plane_z(plane, 0, near_wall.x, near_wall.y);
            const std::int64_t own_b = plane_z(plane, 0, far_wall.x, far_wall.y);
            const std::int64_t other_a = plane_z(plane, neighbour, near_wall.x, near_wall.y);
            const std::int64_t other_b = plane_z(plane, neighbour, far_wall.x, far_wall.y);
            const std::int64_t bound_a = kind == SurfaceKind::PortalUpper
                                             ? std::max(own_a, other_a)
                                             : std::min(own_a, other_a);
            const std::int64_t bound_b = kind == SurfaceKind::PortalUpper
                                             ? std::max(own_b, other_b)
                                             : std::min(own_b, other_b);
            const bool a_closed = own_a == bound_a;
            const bool b_closed = own_b == bound_b;
            CHECK(a_closed != b_closed); // exactly one, or it would not be a wedge
            if (a_closed) {
                ++closed_at_a;
            } else {
                ++closed_at_b;
            }
        }
    }
    CHECK(closed_at_a == 2); // upper and lower on one wall
    CHECK(closed_at_b == 2); // upper and lower on the other

    // Wall 2 runs PARALLEL to the hinge, so both endpoints sit at the same
    // perpendicular distance and the span closes along its whole length.
    // Omitted entirely -- not emitted as a pair of zero-area triangles.
    CHECK(span_for(SurfaceKind::PortalUpper, 2) == nullptr);
    CHECK(span_for(SurfaceKind::PortalLower, 2) == nullptr);
}

TEST_CASE("no emitted triangle anywhere has zero area") {
    // The blunt version of the same guarantee, across every fixture that
    // slopes: a degenerate triangle must never reach a consumer.
    for (const char* name : {"portal_slope_collapse", "ramp_floor_pos", "ramp_floor_neg",
                             "ramp_ceiling", "slope_wide_z_pos", "two_sector_portal",
                             "portal_heights", "portal_step_floor", "multi_loop"}) {
        const StructuralWorld world = build_fixture(name);
        INFO("fixture: " << name);
        check_triangles_wellformed(world);
    }
}

TEST_CASE("an undefined slope plane omits only what depends on it") {
    // D0019 (accepted). A flagged plane whose first-wall hinge has zero length
    // has no defined slope plane. It is never flattened as a substitute and it
    // never aborts the rest of an otherwise valid world: what goes is exactly
    // the geometry whose placement needs that plane.
    //
    // The fixture is a square with a duplicated first vertex, so the polygon
    // keeps its AREA -- otherwise D0018's zero-area rule would omit both
    // planes and this test would pass for the wrong reason. Its floor is
    // sloped with the degenerate hinge; its ceiling is ordinary and flat.
    auto map = map_fixture("slope_degenerate_hinge");
    REQUIRE(map.is_ok());
    const auto& sector = map.value().sectors[0];
    CHECK((sector.floorstat & fauxbuild::mapv7::kStatSloped) != 0);
    CHECK(sector.floorheinum == 4096);
    CHECK((sector.ceilingstat & fauxbuild::mapv7::kStatSloped) == 0); // ordinary ceiling
    CHECK(map.value().walls[0].x == map.value().walls[1].x);
    CHECK(map.value().walls[0].y == map.value().walls[1].y);

    CHECK_FALSE(slope_is_evaluable(map.value(), 0, SurfacePlane::Floor));
    CHECK(slope_is_evaluable(map.value(), 0, SurfacePlane::Ceiling));

    auto built = build_structural_world(map.value());
    REQUIRE(built.is_ok()); // the world is NOT aborted
    const StructuralWorld world = built.take();

    // The diagnostic names the sector and the affected plane, and only it.
    std::size_t degenerate_diagnostics = 0;
    for (const auto& d : world.diagnostics) {
        if (d.reason == "slope_hinge_degenerate") {
            ++degenerate_diagnostics;
            CHECK(d.record == "sector[0]");
            CHECK(d.surface == "floor");
        }
    }
    CHECK(degenerate_diagnostics == 1);

    std::size_t floors = 0;
    std::size_t ceilings = 0;
    std::size_t spans = 0;
    for (const auto& surface : world.surfaces) {
        switch (surface.kind) {
        case SurfaceKind::Floor:
            ++floors;
            break;
        case SurfaceKind::Ceiling:
            ++ceilings;
            break;
        default:
            ++spans;
            break;
        }
    }
    CHECK(floors == 0);   // depends on the undefined plane
    CHECK(ceilings == 1); // independently derivable: RETAINED
    // Solid spans need both of this sector's planes for their endpoints, and
    // fabricating a base-Z endpoint is exactly what D0019 forbids.
    CHECK(spans == 0);

    // The retained ceiling is real geometry at its authored height, not a
    // placeholder.
    const StructuralSurface* ceiling = find_surface(world, SurfaceKind::Ceiling, 0, -1);
    REQUIRE(ceiling != nullptr);
    CHECK(ceiling->indices.size() >= 3);
    for (const auto& v : ceiling->vertices) {
        CHECK(v.y == 32768.0 / 32768.0); // ceilingz -32768
    }
}

TEST_CASE("sloped geometry comes from the evaluator, and walls meet it exactly") {
    // E. Every sloped surface vertex must equal the evaluator at its own XY.
    //    Reading the emitted world back and re-deriving through surface_z_at
    //    is what catches a second, divergent equation in generation.
    for (const char* name : {"ramp_floor_pos", "ramp_floor_neg", "ramp_ceiling"}) {
        auto map = map_fixture(name);
        REQUIRE(map.is_ok());
        const StructuralWorld world = build_fixture(name);
        check_triangles_wellformed(world);

        bool saw_non_base = false;
        for (const auto& surface : world.surfaces) {
            const bool is_ceiling = surface.kind == SurfaceKind::Ceiling;
            if (surface.kind != SurfaceKind::Floor && !is_ceiling) {
                continue;
            }
            const SurfacePlane plane = is_ceiling ? SurfacePlane::Ceiling : SurfacePlane::Floor;
            for (const auto& v : surface.vertices) {
                // Render space back to Build XY: x * 2048, z * 2048.
                const auto bx = static_cast<std::int32_t>(std::llround(v.x * 2048.0));
                const auto by = static_cast<std::int32_t>(std::llround(v.z * 2048.0));
                const std::int64_t expected =
                    surface_z_at(map.value(), surface.sector, plane, bx, by);
                const StructuralVertex want =
                    to_render_space(bx, by, static_cast<std::int32_t>(expected));
                CHECK(v.y == want.y);
                if (expected != (is_ceiling ? map.value().sectors[0].ceilingz
                                            : map.value().sectors[0].floorz)) {
                    saw_non_base = true;
                }
            }
        }
        CHECK(saw_non_base); // the fixture really does slope
    }

    // F. Wall seam: a wall's endpoints must sit exactly on the sloped planes
    //    at the same XY, so no crack opens between a wall and its floor.
    auto map = map_fixture("ramp_floor_pos");
    REQUIRE(map.is_ok());
    const StructuralWorld world = build_fixture("ramp_floor_pos");
    std::size_t checked = 0;
    for (const auto& surface : world.surfaces) {
        if (surface.kind != SurfaceKind::SolidWall) {
            continue;
        }
        for (const auto& v : surface.vertices) {
            const auto bx = static_cast<std::int32_t>(std::llround(v.x * 2048.0));
            const auto by = static_cast<std::int32_t>(std::llround(v.z * 2048.0));
            const std::int64_t floor_z =
                surface_z_at(map.value(), surface.sector, SurfacePlane::Floor, bx, by);
            const std::int64_t ceiling_z =
                surface_z_at(map.value(), surface.sector, SurfacePlane::Ceiling, bx, by);
            const StructuralVertex on_floor =
                to_render_space(bx, by, static_cast<std::int32_t>(floor_z));
            const StructuralVertex on_ceiling =
                to_render_space(bx, by, static_cast<std::int32_t>(ceiling_z));
            CHECK((v.y == on_floor.y || v.y == on_ceiling.y));
            ++checked;
        }
    }
    CHECK(checked == 12); // three walls, four corners each

    // The seam test must not be passing merely because everything is flat.
    // Deliberately direction-agnostic: which way +heinum moves the surface is
    // the documented sign convention, not something this test should restate.
    std::vector<double> wall_heights;
    for (const auto& surface : world.surfaces) {
        if (surface.kind != SurfaceKind::SolidWall) {
            continue;
        }
        for (const auto& v : surface.vertices) {
            wall_heights.push_back(v.y);
        }
    }
    std::sort(wall_heights.begin(), wall_heights.end());
    wall_heights.erase(std::unique(wall_heights.begin(), wall_heights.end()), wall_heights.end());
    // Flat walls would give exactly two heights (base floor, base ceiling).
    CHECK(wall_heights.size() > 2);
}

TEST_CASE("masked portal walls: the opening layer, spans coexist, appearance raw") {
    // M6.2C1. The masked_wall fixture is the windowed portal: from A's side
    // wall 1 emits upper [0,4096], masked [4096,8192] and lower [8192,16384];
    // from B's side wall 7 emits ONLY the masked layer (B's spans lie inside
    // the opening). The masked layer is a distinct kind, not a re-encoding of
    // PortalUpper/PortalLower, and the appearance stays raw: picnum is NOT
    // overwritten with overpicnum here.
    const StructuralWorld world = build_fixture("masked_wall");
    check_triangles_wellformed(world);

    CHECK(count_kind(world, SurfaceKind::PortalUpper) == 1);
    CHECK(count_kind(world, SurfaceKind::PortalLower) == 1);
    CHECK(count_kind(world, SurfaceKind::PortalMasked) == 2);
    const StructuralSurface* a_masked = find_surface(world, SurfaceKind::PortalMasked, 0, 1);
    const StructuralSurface* b_masked = find_surface(world, SurfaceKind::PortalMasked, 1, 7);
    REQUIRE(a_masked != nullptr);
    REQUIRE(b_masked != nullptr);

    // Open at both endpoints -> quad: 4 vertices, 2 triangles.
    for (const StructuralSurface* masked : {a_masked, b_masked}) {
        CHECK(masked->vertices.size() == 4);
        CHECK(masked->indices.size() == 6);
        // Appearance preserved raw, verbatim from the wall record.
        CHECK(masked->appearance.picnum == 0);
        CHECK(masked->appearance.overpicnum == 210);
        CHECK(masked->appearance.raw_stat == 0x0010);
    }

    // The masked layer spans exactly the window: Build Z [4096, 8192] over
    // the vertical scale 2048*16 = 32768 -> render Y [-0.25, -0.125].
    auto y_extent = [](const StructuralSurface& s) {
        double lo = s.vertices[0].y, hi = s.vertices[0].y;
        for (const auto& v : s.vertices) {
            lo = v.y < lo ? v.y : lo;
            hi = v.y > hi ? v.y : hi;
        }
        return std::pair<double, double>{lo, hi};
    };
    const auto [mlo, mhi] = y_extent(*a_masked);
    CHECK(mlo == -8192.0 / 32768.0);
    CHECK(mhi == -4096.0 / 32768.0);

    // Canonical order per wall: portal_upper, then portal_lower, then
    // portal_masked.
    std::size_t upper_at = 0, lower_at = 0, masked_at = 0;
    for (std::size_t i = 0; i < world.surfaces.size(); ++i) {
        if (world.surfaces[i].kind == SurfaceKind::PortalUpper && world.surfaces[i].wall == 1) {
            upper_at = i;
        }
        if (world.surfaces[i].kind == SurfaceKind::PortalLower && world.surfaces[i].wall == 1) {
            lower_at = i;
        }
        if (world.surfaces[i].kind == SurfaceKind::PortalMasked && world.surfaces[i].sector == 0) {
            masked_at = i;
        }
    }
    CHECK(upper_at < lower_at);
    CHECK(lower_at < masked_at);

    // The feature is implemented; masked portal walls carry no deferral note.
    for (const auto& note : world.notes) {
        CHECK(note.detail.find("masked") == std::string::npos);
    }
}

TEST_CASE("masked layer on an equal-height portal spans the full opening") {
    // Equal, properly ordered sectors (ceiling 0, floor 16384): the opening
    // IS the whole wall, so the masked layer is a full-height quad — no
    // upper/lower spans exist to bound it. (two_sector_portal itself carries
    // the make_sector default inverted interval, which has no opening; this
    // case needs the real-content ordering.)
    fauxbuild::mapv7::MapData map;
    const std::int32_t u = 1024;
    const std::int32_t ax[] = {0, u, u, 0};
    const std::int32_t ay[] = {0, 0, u, u};
    for (std::size_t i = 0; i < 4; ++i) {
        map.walls.push_back(fauxbuild::mapv7::Wall());
        map.walls[i].x = ax[i];
        map.walls[i].y = ay[i];
        map.walls[i].point2 = static_cast<std::int16_t>((i + 1) % 4);
    }
    const std::int32_t bx[] = {u, 2 * u, 2 * u, u};
    const std::int32_t by[] = {0, 0, u, u};
    for (std::size_t i = 0; i < 4; ++i) {
        map.walls.push_back(fauxbuild::mapv7::Wall());
        map.walls[4 + i].x = bx[i];
        map.walls[4 + i].y = by[i];
        map.walls[4 + i].point2 = static_cast<std::int16_t>(4 + (i + 1) % 4);
    }
    map.walls[1].nextwall = 7;
    map.walls[1].nextsector = 1;
    map.walls[7].nextwall = 1;
    map.walls[7].nextsector = 0;
    for (int s = 0; s < 2; ++s) {
        map.sectors.push_back(fauxbuild::mapv7::Sector());
        map.sectors[static_cast<std::size_t>(s)].wallptr = static_cast<std::int16_t>(4 * s);
        map.sectors[static_cast<std::size_t>(s)].wallnum = 4;
        map.sectors[static_cast<std::size_t>(s)].floorz = 16384;
        map.sectors[static_cast<std::size_t>(s)].ceilingz = 0;
    }
    for (const std::int16_t w : {std::int16_t{1}, std::int16_t{7}}) {
        map.walls[static_cast<std::size_t>(w)].cstat = fauxbuild::mapv7::kWallCstatMasked;
        map.walls[static_cast<std::size_t>(w)].overpicnum = 9;
    }
    auto world = build_structural_world(map);
    REQUIRE(world.is_ok());

    check_triangles_wellformed(world.value());
    CHECK(count_kind(world.value(), SurfaceKind::PortalUpper) == 0);
    CHECK(count_kind(world.value(), SurfaceKind::PortalLower) == 0);
    CHECK(count_kind(world.value(), SurfaceKind::PortalMasked) == 2);
    CHECK(count_kind(world.value(), SurfaceKind::SolidWall) == 6);
    for (const StructuralSurface& s : world.value().surfaces) {
        if (s.kind == SurfaceKind::PortalMasked) {
            CHECK(s.vertices.size() == 4);
            CHECK(s.indices.size() == 6);
            CHECK(s.appearance.overpicnum == 9);
            double lo = s.vertices[0].y, hi = s.vertices[0].y;
            for (const auto& v : s.vertices) {
                lo = v.y < lo ? v.y : lo;
                hi = v.y > hi ? v.y : hi;
            }
            CHECK(lo == -16384.0 / 32768.0);
            CHECK(hi == 0.0);
        }
    }
}

TEST_CASE("masked layer collapses to a wedge when a slope closes the opening at one end") {
    // The opening endpoints come from the same plane evaluations as the
    // upper/lower spans: B's floor slopes up to meet the shared ceiling at
    // the far endpoint, so the masked layer degenerates to ONE triangle
    // exactly like any other span (no zero-area triangle reaches a consumer).
    fauxbuild::mapv7::MapData map;
    const std::int32_t u = 1024;
    const std::int32_t ax[] = {0, u, u, 0};
    const std::int32_t ay[] = {0, 0, u, u};
    for (std::size_t i = 0; i < 4; ++i) {
        map.walls.push_back(fauxbuild::mapv7::Wall());
        map.walls[i].x = ax[i];
        map.walls[i].y = ay[i];
        map.walls[i].point2 = static_cast<std::int16_t>((i + 1) % 4);
    }
    const std::int32_t bx[] = {u, 2 * u, 2 * u, u};
    const std::int32_t by[] = {0, 0, u, u};
    for (std::size_t i = 0; i < 4; ++i) {
        map.walls.push_back(fauxbuild::mapv7::Wall());
        map.walls[4 + i].x = bx[i];
        map.walls[4 + i].y = by[i];
        map.walls[4 + i].point2 = static_cast<std::int16_t>(4 + (i + 1) % 4);
    }
    map.walls[1].nextwall = 7;
    map.walls[1].nextsector = 1;
    map.walls[7].nextwall = 1;
    map.walls[7].nextsector = 0;
    // A: flat [0, 16384]. B: flat ceiling 0, floor base 16384 sloping about
    // B's first wall (u,0)->(2u,0) with heinum -4096: evaluated floor is
    // 16384 - 16y, reaching 0 (the ceiling) at y = 1024.
    map.sectors.push_back(fauxbuild::mapv7::Sector());
    map.sectors[0].wallptr = 0;
    map.sectors[0].wallnum = 4;
    map.sectors[0].floorz = 16384;
    map.sectors[0].ceilingz = 0;
    map.sectors.push_back(fauxbuild::mapv7::Sector());
    map.sectors[1].wallptr = 4;
    map.sectors[1].wallnum = 4;
    map.sectors[1].floorz = 16384;
    map.sectors[1].ceilingz = 0;
    map.sectors[1].floorstat = fauxbuild::mapv7::kStatSloped;
    map.sectors[1].floorheinum = -4096;
    for (const std::int16_t w : {std::int16_t{1}, std::int16_t{7}}) {
        map.walls[static_cast<std::size_t>(w)].cstat = fauxbuild::mapv7::kWallCstatMasked;
        map.walls[static_cast<std::size_t>(w)].overpicnum = 4;
    }

    auto world = build_structural_world(map);
    REQUIRE(world.is_ok());
    check_triangles_wellformed(world.value());
    CHECK(count_kind(world.value(), SurfaceKind::PortalMasked) == 2);
    for (const StructuralSurface& s : world.value().surfaces) {
        if (s.kind != SurfaceKind::PortalMasked) {
            continue;
        }
        INFO("sector ", s.sector, " wall ", s.wall);
        CHECK(s.vertices.size() == 3);
        CHECK(s.indices.size() == 3);
        // The surviving corner is the OPEN endpoint (y = 0, Build Z floor
        // 16384); the collapsed corner (y = 1024) contributes one vertex.
        double lo = s.vertices[0].y, hi = s.vertices[0].y;
        for (const auto& v : s.vertices) {
            lo = v.y < lo ? v.y : lo;
            hi = v.y > hi ? v.y : hi;
        }
        CHECK(lo == doctest::Approx(-16384.0 / 32768.0).epsilon(1e-12));
        CHECK(hi == 0.0);
    }
}

TEST_CASE("masked layer is omitted when the opening is closed at both endpoints") {
    // B's vertical interval is inverted (floor 4096 above ceiling 8192
    // numerically), so the shared opening has no positive extent and the
    // masked bit must not manufacture a layer any more than it manufactures
    // upper/lower spans.
    fauxbuild::mapv7::MapData map;
    const std::int32_t u = 1024;
    const std::int32_t ax[] = {0, u, u, 0};
    const std::int32_t ay[] = {0, 0, u, u};
    for (std::size_t i = 0; i < 4; ++i) {
        map.walls.push_back(fauxbuild::mapv7::Wall());
        map.walls[i].x = ax[i];
        map.walls[i].y = ay[i];
        map.walls[i].point2 = static_cast<std::int16_t>((i + 1) % 4);
    }
    const std::int32_t bx[] = {u, 2 * u, 2 * u, u};
    const std::int32_t by[] = {0, 0, u, u};
    for (std::size_t i = 0; i < 4; ++i) {
        map.walls.push_back(fauxbuild::mapv7::Wall());
        map.walls[4 + i].x = bx[i];
        map.walls[4 + i].y = by[i];
        map.walls[4 + i].point2 = static_cast<std::int16_t>(4 + (i + 1) % 4);
    }
    map.walls[1].nextwall = 7;
    map.walls[1].nextsector = 1;
    map.walls[7].nextwall = 1;
    map.walls[7].nextsector = 0;
    map.sectors.push_back(fauxbuild::mapv7::Sector());
    map.sectors[0].wallptr = 0;
    map.sectors[0].wallnum = 4;
    map.sectors[0].floorz = 16384;
    map.sectors[0].ceilingz = 0;
    map.sectors.push_back(fauxbuild::mapv7::Sector());
    map.sectors[1].wallptr = 4;
    map.sectors[1].wallnum = 4;
    map.sectors[1].floorz = 4096;
    map.sectors[1].ceilingz = 8192;
    for (const std::int16_t w : {std::int16_t{1}, std::int16_t{7}}) {
        map.walls[static_cast<std::size_t>(w)].cstat = fauxbuild::mapv7::kWallCstatMasked;
        map.walls[static_cast<std::size_t>(w)].overpicnum = 4;
    }

    auto world = build_structural_world(map);
    REQUIRE(world.is_ok());
    CHECK(count_kind(world.value(), SurfaceKind::PortalMasked) == 0);
    // The upper/lower spans legitimately survive (B's planes sit inside A's
    // interval, so the wall solidifies above and below the vanished
    // opening); only the masked layer has nothing to span.
    CHECK(count_kind(world.value(), SurfaceKind::PortalUpper) == 1);
    CHECK(count_kind(world.value(), SurfaceKind::PortalLower) == 1);
}

TEST_CASE("converse trap: a nonzero overpicnum alone never creates a masked layer") {
    // 305 non-masked walls across the six owned maps carry a nonzero
    // overpicnum (E1L1: 60 of 79). The masked BIT selects the layer; the
    // field is preserved verbatim and consumed by nothing on ordinary kinds.
    auto map = map_fixture("portal_heights");
    REQUIRE(map.is_ok());
    for (const std::int16_t w : {std::int16_t{1}, std::int16_t{7}}) {
        map.value().walls[static_cast<std::size_t>(w)].overpicnum = 77;
    }
    auto world = build_structural_world(map.value());
    REQUIRE(world.is_ok());

    CHECK(count_kind(world.value(), SurfaceKind::PortalMasked) == 0);
    CHECK(count_kind(world.value(), SurfaceKind::PortalUpper) == 1);
    CHECK(count_kind(world.value(), SurfaceKind::PortalLower) == 1);
    const StructuralSurface* upper = find_surface(world.value(), SurfaceKind::PortalUpper, 0, 1);
    REQUIRE(upper != nullptr);
    CHECK(upper->appearance.overpicnum == 77); // preserved, not consumed
    CHECK(upper->appearance.picnum == 0);
}

TEST_CASE("masked bit on a solid wall reports and manufactures nothing") {
    // A solid wall has no portal opening; no semantics are invented for the
    // bit there. It is preserved raw and reported with a note.
    auto map = map_fixture("square_room");
    REQUIRE(map.is_ok());
    map.value().walls[0].cstat = fauxbuild::mapv7::kWallCstatMasked;
    map.value().walls[0].overpicnum = 5;
    auto world = build_structural_world(map.value());
    REQUIRE(world.is_ok());

    CHECK(count_kind(world.value(), SurfaceKind::PortalMasked) == 0);
    const StructuralSurface* solid = find_surface(world.value(), SurfaceKind::SolidWall, 0, 0);
    REQUIRE(solid != nullptr);
    CHECK(solid->appearance.raw_stat == fauxbuild::mapv7::kWallCstatMasked);
    CHECK(solid->appearance.overpicnum == 5);
    bool noted = false;
    for (const auto& note : world.value().notes) {
        if (note.record == "wall[0]" &&
            note.detail.find("masked bit on a non-portal wall") != std::string::npos) {
            noted = true;
        }
    }
    CHECK(noted);
}

TEST_CASE("setting the masked bit changes nothing about any other surface") {
    // The masked layer is purely additive: same world with and without the
    // bit, every non-PortalMasked surface must be identical — geometry,
    // indices and raw appearance all byte-for-byte.
    auto plain_map = map_fixture("portal_heights");
    auto masked_map = map_fixture("portal_heights");
    REQUIRE(plain_map.is_ok());
    REQUIRE(masked_map.is_ok());
    for (const std::int16_t w : {std::int16_t{1}, std::int16_t{7}}) {
        masked_map.value().walls[static_cast<std::size_t>(w)].cstat =
            fauxbuild::mapv7::kWallCstatMasked; // overpicnum stays 0: tile 0, no sentinel
    }
    auto plain = build_structural_world(plain_map.value());
    auto masked = build_structural_world(masked_map.value());
    REQUIRE(plain.is_ok());
    REQUIRE(masked.is_ok());

    std::size_t plain_at = 0;
    for (const auto& surface : masked.value().surfaces) {
        if (surface.kind == SurfaceKind::PortalMasked) {
            continue; // the additive layer itself
        }
        REQUIRE(plain_at < plain.value().surfaces.size());
        const StructuralSurface& before = plain.value().surfaces[plain_at++];
        CHECK(surface.kind == before.kind);
        CHECK(surface.sector == before.sector);
        CHECK(surface.wall == before.wall);
        CHECK(surface.vertices == before.vertices);
        CHECK(surface.indices == before.indices);
        // The one legitimate appearance difference: the authored bit itself
        // lands in raw_stat of the OTHER spans of the same wall. Clearing it
        // must yield exactly the plain world's appearance — any further
        // difference (a rewritten picnum, a moved pan) is a real regression.
        fauxbuild::SurfaceAppearance got = surface.appearance;
        got.raw_stat =
            static_cast<std::int16_t>(got.raw_stat & ~fauxbuild::mapv7::kWallCstatMasked);
        CHECK(got == before.appearance);
    }
    CHECK(plain_at == plain.value().surfaces.size());
    CHECK(count_kind(masked.value(), SurfaceKind::PortalMasked) == 2);
}

// ---------------------------------------------------------------------------
// I. M6 slice 1: raw appearance contract and the slope-flag pin
// ---------------------------------------------------------------------------

TEST_CASE("appearance facts are preserved raw on emitted surfaces") {
    // The consumer is the pure-C++ StructuralWorld: read the emitted surfaces
    // themselves, field by field, against the source MAP records. No
    // interpretation may creep in: every value below is the source field
    // verbatim (M6 slice 1 §8).
    auto map = map_fixture("square_room");
    REQUIRE(map.is_ok());
    auto& sector = map.value().sectors[0];
    // Distinct, deliberately awkward source values (negative shade, nonzero
    // panning/pal) so a transposed or truncated field is loud.
    sector.ceilingpicnum = 111;
    sector.floorpicnum = 222;
    sector.ceilingshade = -13;
    sector.floorshade = 17;
    sector.ceilingpal = 3;
    sector.floorpal = 4;
    sector.ceilingxpanning = 11;
    sector.ceilingypanning = 13;
    sector.floorxpanning = 17;
    sector.floorypanning = 19;
    sector.ceilingstat = 0x0044; // uninterpreted bits stay raw (slope bit clear)
    sector.floorstat = 0x0018;
    for (auto& wall : map.value().walls) {
        wall.picnum = 333;
        wall.overpicnum = 444;
        wall.cstat = 0x0108;
        wall.shade = -5;
        wall.pal = 6;
        wall.xrepeat = 9;
        wall.yrepeat = 10;
        wall.xpanning = 21;
        wall.ypanning = 23;
    }
    const StructuralWorld world = build_structural_world(map.value()).value();

    const StructuralSurface* floor = find_surface(world, SurfaceKind::Floor, 0, -1);
    REQUIRE(floor != nullptr);
    CHECK(floor->appearance.picnum == 222);
    CHECK(floor->appearance.overpicnum == 0); // not a wall field
    CHECK(floor->appearance.raw_stat == 0x0018);
    CHECK(floor->appearance.shade == 17);
    CHECK(floor->appearance.pal == 4);
    CHECK(floor->appearance.xpanning == 17);
    CHECK(floor->appearance.ypanning == 19);
    CHECK(floor->appearance.xrepeat == 0); // not a floor/ceiling field
    CHECK(floor->appearance.yrepeat == 0);

    const StructuralSurface* ceiling = find_surface(world, SurfaceKind::Ceiling, 0, -1);
    REQUIRE(ceiling != nullptr);
    CHECK(ceiling->appearance.picnum == 111);
    CHECK(ceiling->appearance.raw_stat == 0x0044);
    CHECK(ceiling->appearance.shade == -13);
    CHECK(ceiling->appearance.pal == 3);
    CHECK(ceiling->appearance.xpanning == 11);
    CHECK(ceiling->appearance.ypanning == 13);

    for (std::int16_t w = 0; w < 4; ++w) {
        const StructuralSurface* wall = find_surface(world, SurfaceKind::SolidWall, 0, w);
        REQUIRE(wall != nullptr);
        CHECK(wall->appearance.picnum == 333);
        CHECK(wall->appearance.overpicnum == 444);
        CHECK(wall->appearance.raw_stat == 0x0108);
        CHECK(wall->appearance.shade == -5);
        CHECK(wall->appearance.pal == 6);
        CHECK(wall->appearance.xrepeat == 9);
        CHECK(wall->appearance.yrepeat == 10);
        CHECK(wall->appearance.xpanning == 21);
        CHECK(wall->appearance.ypanning == 23);
    }

    // Geometry is untouched by appearance: the floor plane stays at its base
    // Z (no slope bit set; heinum irrelevant here and zero).
    for (const auto& v : floor->vertices) {
        CHECK(v.y == 0.0);
    }
}

TEST_CASE("heinum without the slope flag stays perfectly flat") {
    // M6 slice 1 §2 pin (fixture matrix B): M3 established that stat 0x0002
    // marks a slope and that a nonzero heinum without it is an ignored
    // leftover in real content (n=4,900 surfaces). Geometry must honour the
    // flag, never the heinum alone — this case stays green after the slope
    // evaluator lands, because flag-clear sectors remain flat by definition.
    auto map = map_fixture("minimal");
    REQUIRE(map.is_ok());
    map.value().sectors[0].floorheinum = 1000;
    map.value().sectors[0].ceilingheinum = -1000;
    // Flag deliberately NOT set.
    const StructuralWorld world = build_structural_world(map.value()).value();

    for (const auto& note : world.notes) {
        CHECK(note.detail.find("slope present") == std::string::npos);
    }
    const StructuralSurface* floor = find_surface(world, SurfaceKind::Floor, 0, -1);
    REQUIRE(floor != nullptr);
    for (const auto& v : floor->vertices) {
        CHECK(v.y == 0.0);
    }
    const StructuralSurface* ceiling = find_surface(world, SurfaceKind::Ceiling, 0, -1);
    REQUIRE(ceiling != nullptr);
    for (const auto& v : ceiling->vertices) {
        CHECK(v.y == -16384.0 / 32768.0);
    }
}

TEST_CASE("raw sector visibility reaches the consumer through StructuralWorld") {
    // M6 preserves the value verbatim; M10 owns its behavioural and render
    // interpretation. The point of the test is index correspondence, so the
    // sectors are given distinct visibilities before deriving -- a table that
    // is merely the right length, or that copies one value everywhere, must
    // not pass.
    auto map = map_fixture("two_sector_portal");
    REQUIRE(map.is_ok());
    REQUIRE(map.value().sectors.size() >= 2);
    auto source = map.take();
    for (std::size_t i = 0; i < source.sectors.size(); ++i) {
        source.sectors[i].visibility = static_cast<std::uint8_t>(17 + 5 * i);
    }

    auto built = build_structural_world(source);
    REQUIRE(built.is_ok());
    const StructuralWorld world = built.take();

    REQUIRE(world.sector_appearance.size() == source.sectors.size());
    for (std::size_t i = 0; i < source.sectors.size(); ++i) {
        CHECK(world.sector_appearance[i].visibility == source.sectors[i].visibility);
    }
    // Distinct by construction, so a broadcast copy cannot satisfy the above.
    CHECK(world.sector_appearance[0].visibility != world.sector_appearance[1].visibility);

    // A surface indexes its own sector's entry directly.
    for (const auto& surface : world.surfaces) {
        REQUIRE(surface.sector >= 0);
        REQUIRE(static_cast<std::size_t>(surface.sector) < world.sector_appearance.size());
        CHECK(world.sector_appearance[static_cast<std::size_t>(surface.sector)].visibility ==
              source.sectors[static_cast<std::size_t>(surface.sector)].visibility);
    }

    // The table is total: every source sector has an entry even when a sector
    // emits no surface at all.
    const StructuralWorld degenerate = build_fixture("square_room");
    auto square = map_fixture("square_room");
    REQUIRE(square.is_ok());
    CHECK(degenerate.sector_appearance.size() == square.value().sectors.size());
    CHECK(degenerate.sector_appearance[0].visibility == square.value().sectors[0].visibility);
}

// ---------------------------------------------------------------------------
// Synthetic reductions of the failure classes the bespoke ear clipper produced
// on legally owned content (M5 slice-1 amendment, item E). These are ORIGINAL
// geometry: each reproduces the *shape* of a real failure without copying any
// proprietary coordinate. They are the CI proof — the local six-map scan is
// supporting evidence only, and no map name, sector number or count appears in
// engine code or here.
// ---------------------------------------------------------------------------
namespace {

// Build a sector from explicit loops. Each loop is a closed ring; the first is
// the outer boundary, the rest are holes.
fauxbuild::mapv7::MapData
sector_from_loops(const std::vector<std::vector<std::pair<std::int32_t, std::int32_t>>>& loops) {
    fauxbuild::mapv7::MapData map;
    using fauxbuild::synth::make_sector;
    using fauxbuild::synth::make_wall;
    std::int16_t base = 0;
    for (const auto& loop : loops) {
        const auto n = static_cast<std::int16_t>(loop.size());
        for (std::int16_t i = 0; i < n; ++i) {
            map.walls.push_back(make_wall(loop[static_cast<std::size_t>(i)].first,
                                          loop[static_cast<std::size_t>(i)].second,
                                          static_cast<std::int16_t>(base + (i + 1) % n)));
        }
        base = static_cast<std::int16_t>(base + n);
    }
    map.sectors.push_back(make_sector(0, static_cast<std::int16_t>(map.walls.size())));
    map.start = {0, 0, 4096, 512, 0};
    return map;
}

double total_floor_area(const StructuralWorld& world) {
    double area = 0.0;
    for (const auto& surface : world.surfaces) {
        if (surface.kind != SurfaceKind::Floor) {
            continue;
        }
        for (std::size_t t = 0; t + 2 < surface.indices.size(); t += 3) {
            const auto& a = surface.vertices[surface.indices[t]];
            const auto& b = surface.vertices[surface.indices[t + 1]];
            const auto& c = surface.vertices[surface.indices[t + 2]];
            area += std::abs(cross2(b.x - a.x, b.z - a.z, c.x - a.x, c.z - a.z)) / 2.0;
        }
    }
    return area;
}

} // namespace

TEST_CASE("deep concave comb boundary triangulates") {
    // Coverage for heavily concave boundaries. NOTE: verified against the
    // former bespoke clipper and it handled this shape, so this is not a
    // reduction of the "no valid ear remains" class -- see the spiral case
    // below for a shape that does reproduce it.
    const std::int32_t u = kUnit;
    std::vector<std::pair<std::int32_t, std::int32_t>> outer;
    outer.push_back({0, 0});
    for (int i = 0; i < 6; ++i) { // teeth along the top edge
        const std::int32_t x = u + static_cast<std::int32_t>(i) * 2 * u;
        outer.push_back({x, 0});
        outer.push_back({x + u / 2, 3 * u});
        outer.push_back({x + u, 0});
    }
    outer.push_back({13 * u, 0});
    outer.push_back({13 * u, 4 * u});
    outer.push_back({0, 4 * u});
    auto world = build_structural_world(sector_from_loops({outer}));
    REQUIRE(world.is_ok());
    CHECK(count_kind(world.value(), SurfaceKind::Floor) == 1);
    CHECK(world.value().diagnostics.empty());
    CHECK(total_floor_area(world.value()) > 0.0);
}

TEST_CASE("a hole authored in the same winding as the outer loop is normalised") {
    // Winding normalisation is by role, never by trusting source order. NOTE:
    // the former clipper also handled this, so it does not reduce the real
    // "residual winding flipped" class; that class is covered by the local
    // six-map scan as supporting evidence only.
    const std::int32_t u = kUnit;
    const std::vector<std::pair<std::int32_t, std::int32_t>> outer = {
        {0, 0}, {4 * u, 0}, {4 * u, 4 * u}, {0, 4 * u}};
    const std::vector<std::pair<std::int32_t, std::int32_t>> hole_ccw = {
        {u, u}, {2 * u, u}, {2 * u, 2 * u}, {u, 2 * u}};
    auto world = build_structural_world(sector_from_loops({outer, hole_ccw}));
    REQUIRE(world.is_ok());
    // 16 square units of outer minus 1 of hole, in render units (u -> 32).
    CHECK(total_floor_area(world.value()) == doctest::Approx(15.0 * 32.0 * 32.0));
    CHECK_FALSE(covered_by_floor(world.value(), 0, 1.5 * 32.0, 1.5 * 32.0));
}

TEST_CASE("REDUCTION: hole vertex resting on a horizontal outer edge is valid") {
    // The configuration real content actually uses, and the one the old rule
    // wrongly rejected as "not strictly inside". A point-in-polygon test that
    // skips horizontal edges at the probe height reports it Outside unless it
    // coincides with a vertex — which is why this reduction exists.
    const std::int32_t u = kUnit;
    const std::vector<std::pair<std::int32_t, std::int32_t>> outer = {
        {0, 0}, {4 * u, 0}, {4 * u, 4 * u}, {0, 4 * u}};
    const std::vector<std::pair<std::int32_t, std::int32_t>> hole = {
        {u, 4 * u}, {u, 3 * u}, {3 * u, 3 * u}, {3 * u, 4 * u}}; // touches the top edge
    auto world = build_structural_world(sector_from_loops({outer, hole}));
    REQUIRE(world.is_ok());
    CHECK(world.value().diagnostics.empty());
    CHECK(total_floor_area(world.value()) == doctest::Approx(14.0 * 32.0 * 32.0));
    CHECK_FALSE(covered_by_floor(world.value(), 0, 2.0 * 32.0, 3.5 * 32.0));
}

TEST_CASE("two holes side by side triangulate") {
    // Multi-hole coverage. NOTE: the former clipper handled this too, so it is
    // not a reduction of the real "no visible bridge target" class -- bridging
    // no longer exists at all, which is why that class cannot recur.
    const std::int32_t u = kUnit;
    const std::vector<std::pair<std::int32_t, std::int32_t>> outer = {
        {0, 0}, {6 * u, 0}, {6 * u, 3 * u}, {0, 3 * u}};
    const std::vector<std::pair<std::int32_t, std::int32_t>> a = {
        {u, u}, {2 * u, u}, {2 * u, 2 * u}, {u, 2 * u}};
    const std::vector<std::pair<std::int32_t, std::int32_t>> b = {
        {3 * u, u}, {4 * u, u}, {4 * u, 2 * u}, {3 * u, 2 * u}};
    auto world = build_structural_world(sector_from_loops({outer, a, b}));
    REQUIRE(world.is_ok());
    CHECK(total_floor_area(world.value()) == doctest::Approx(16.0 * 32.0 * 32.0));
    CHECK_FALSE(covered_by_floor(world.value(), 0, 1.5 * 32.0, 1.5 * 32.0));
    CHECK_FALSE(covered_by_floor(world.value(), 0, 3.5 * 32.0, 1.5 * 32.0));
}

TEST_CASE("REDUCTION: a fully collinear sector is a diagnostic, not a failure") {
    // D0018. Real shipped content contains a sector whose walls are collinear,
    // enclosing exactly zero area. It must not cost the rest of the world.
    const std::int32_t u = kUnit;
    auto map = sector_from_loops({{{0, u}, {2 * u, u}, {u, u}}});
    auto world = build_structural_world(map);
    REQUIRE(world.is_ok()); // the world still builds
    CHECK(count_kind(world.value(), SurfaceKind::Floor) == 0);
    CHECK(count_kind(world.value(), SurfaceKind::Ceiling) == 0);
    REQUIRE(world.value().diagnostics.size() == 2);
    CHECK(world.value().diagnostics[0].reason == std::string("zero_area"));
    CHECK(world.value().diagnostics[1].reason == std::string("zero_area"));
    CHECK(world.value().diagnostics[0].record == std::string("sector[0]"));
    // Wall spans are geometrically well defined and must still be emitted.
    CHECK(count_kind(world.value(), SurfaceKind::SolidWall) > 0);
}

TEST_CASE("REDUCTION: a self-intersecting hole fails closed before earcut") {
    const std::int32_t u = kUnit;
    const std::vector<std::pair<std::int32_t, std::int32_t>> outer = {
        {0, 0}, {4 * u, 0}, {4 * u, 4 * u}, {0, 4 * u}};
    const std::vector<std::pair<std::int32_t, std::int32_t>> bowtie_hole = {
        {u, u}, {2 * u, 2 * u}, {2 * u, u}, {u, 2 * u}};
    auto world = build_structural_world(sector_from_loops({outer, bowtie_hole}));
    REQUIRE_FALSE(world.is_ok());
    CHECK(world.error().detail.find("self-intersect") != std::string::npos);
}

TEST_CASE("REDUCTION: a hole meeting the outer boundary at two separate vertices") {
    // Stronger form of the touching case: the hole shares two vertices with the
    // outer boundary, splitting the remaining material. The former clipper
    // rejected this outright ("hole loop vertex not strictly inside").
    const std::int32_t u = kUnit;
    const std::vector<std::pair<std::int32_t, std::int32_t>> outer = {
        {0, 0}, {4 * u, 0}, {4 * u, 4 * u}, {0, 4 * u}};
    const std::vector<std::pair<std::int32_t, std::int32_t>> hole = {
        {0, u}, {2 * u, 2 * u}, {0, 3 * u}};
    auto world = build_structural_world(sector_from_loops({outer, hole}));
    REQUIRE(world.is_ok());
    CHECK(world.value().diagnostics.empty());
    CHECK(total_floor_area(world.value()) > 0.0);
}

TEST_CASE("post-verification rejects a triangulation that does not tile the polygon") {
    // The exact-area oracle is load-bearing, and this is not a simulated
    // failure: earcut genuinely mis-triangulates this valid simple polygon (a
    // tightly wound spiral -- independently checked for self-intersection: zero
    // proper crossings, non-zero area). Without post-verification the world
    // would be built from wrong geometry and nothing would say so. With it, the
    // failure is structured and fatal.
    const std::int32_t u = kUnit;
    const std::vector<std::pair<std::int32_t, std::int32_t>> spiral = {
        {0, 0},         {8 * u, 0},     {8 * u, 8 * u}, {0, 8 * u},     {0, 7 * u},
        {7 * u, 7 * u}, {7 * u, u},     {u, u},         {u, 6 * u},     {6 * u, 6 * u},
        {6 * u, 2 * u}, {2 * u, 2 * u}, {2 * u, 5 * u}, {5 * u, 5 * u}, {5 * u, 3 * u},
        {3 * u, 3 * u}, {3 * u, 4 * u}, {4 * u, 4 * u}};
    auto world = build_structural_world(sector_from_loops({spiral}));
    REQUIRE_FALSE(world.is_ok());
    CHECK(world.error().detail.find("exact area invariant") != std::string::npos);
}

TEST_CASE("outer loop selection is by magnitude, not by winding sign") {
    // Consumer-level regression for magnitude_less. extract_loops picks the
    // outer loop by largest |area|; comparing signs instead selects a small
    // hole over a large outer boundary whenever their windings differ. Tested
    // through build_structural_world rather than the helper, because the
    // failure that matters is loop selection, not the comparison in isolation.
    const std::int32_t u = kUnit;
    // Outer wound CW (negative shoelace), hole wound CCW (positive) -- 16x the
    // area, so any magnitude comparison must prefer it.
    const std::vector<std::pair<std::int32_t, std::int32_t>> outer_cw = {
        {0, 0}, {0, 4 * u}, {4 * u, 4 * u}, {4 * u, 0}};
    const std::vector<std::pair<std::int32_t, std::int32_t>> hole_ccw = {
        {u, u}, {2 * u, u}, {2 * u, 2 * u}, {u, 2 * u}};
    auto world = build_structural_world(sector_from_loops({outer_cw, hole_ccw}));
    REQUIRE(world.is_ok()); // sign comparison failed outright here
    CHECK(world.value().diagnostics.empty());
    // 16 square units minus a 1-unit hole, in render units (u -> 32).
    CHECK(total_floor_area(world.value()) == doctest::Approx(15.0 * 32.0 * 32.0));
    CHECK_FALSE(covered_by_floor(world.value(), 0, 1.5 * 32.0, 1.5 * 32.0));

    SUBCASE("and with both windings reversed") {
        const std::vector<std::pair<std::int32_t, std::int32_t>> outer_ccw = {
            {0, 0}, {4 * u, 0}, {4 * u, 4 * u}, {0, 4 * u}};
        const std::vector<std::pair<std::int32_t, std::int32_t>> hole_cw = {
            {u, u}, {u, 2 * u}, {2 * u, 2 * u}, {2 * u, u}};
        auto flipped = build_structural_world(sector_from_loops({outer_ccw, hole_cw}));
        REQUIRE(flipped.is_ok());
        CHECK(total_floor_area(flipped.value()) == doctest::Approx(15.0 * 32.0 * 32.0));
    }
}

TEST_CASE("render scale validation is total and free of float-to-integer UB") {
    // An earlier implementation tested the scale by casting it (and 1/scale) to
    // uint64, which is undefined behaviour when the value does not fit --
    // UBSan-confirmed on a very large finite scale and, via 1/scale, on a very
    // small positive one. These run under the ASan/UBSan configuration.
    auto accepts = [](double scale) {
        fauxbuild::StructuralOptions options;
        options.scale = scale;
        return build_structural_world(fauxbuild::synth::map_fixture("square_room").value(), options)
            .is_ok();
    };
    SUBCASE("powers of two in a usable range are accepted") {
        CHECK(accepts(1.0 / 2048.0));
        CHECK(accepts(1.0));
        CHECK(accepts(0.5));
        CHECK(accepts(1024.0));
    }
    SUBCASE("non-powers of two are rejected") {
        CHECK_FALSE(accepts(1.0 / 1000.0));
        CHECK_FALSE(accepts(3.0));
        CHECK_FALSE(accepts(0.3));
    }
    SUBCASE("non-finite and non-positive values are rejected") {
        CHECK_FALSE(accepts(std::numeric_limits<double>::infinity()));
        CHECK_FALSE(accepts(-std::numeric_limits<double>::infinity()));
        CHECK_FALSE(accepts(std::numeric_limits<double>::quiet_NaN()));
        CHECK_FALSE(accepts(0.0));
        CHECK_FALSE(accepts(-0.5));
    }
    SUBCASE("extreme magnitudes are rejected without undefined behaviour") {
        CHECK_FALSE(accepts(1e300));
        CHECK_FALSE(accepts(1e-300));
        CHECK_FALSE(accepts(std::numeric_limits<double>::max()));
        // A power of two, but the int32 round trip stops being exact once the
        // products underflow into denormals -- D0016's actual requirement.
        CHECK_FALSE(accepts(std::numeric_limits<double>::denorm_min()));
    }
}
