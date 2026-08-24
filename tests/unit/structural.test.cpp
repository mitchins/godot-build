#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "fauxbuild/map_io.hpp"
#include "fauxbuild/map_synth.hpp"
#include "fauxbuild/map_v7.hpp"
#include "fauxbuild/result.hpp"
#include "fauxbuild/structural.hpp"

// M5 slice 1: structural geometry derivation. Everything here is pure C++:
// no Godot types, no scene, no renderer (slice 2 owns presentation).

using fauxbuild::build_structural_world;
using fauxbuild::StructuralOptions;
using fauxbuild::StructuralSurface;
using fauxbuild::StructuralVertex;
using fauxbuild::StructuralWorld;
using fauxbuild::SurfaceKind;
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

bool point_in_triangle_xz(const StructuralSurface& surface, std::size_t tri, double px, double pz) {
    const auto i0 = surface.indices[tri * 3 + 0];
    const auto i1 = surface.indices[tri * 3 + 1];
    const auto i2 = surface.indices[tri * 3 + 2];
    const double d1 = cross2(surface.vertices[i1].x - surface.vertices[i0].x,
                             surface.vertices[i1].z - surface.vertices[i0].z,
                             px - surface.vertices[i0].x, pz - surface.vertices[i0].z);
    const double d2 = cross2(surface.vertices[i2].x - surface.vertices[i1].x,
                             surface.vertices[i2].z - surface.vertices[i1].z,
                             px - surface.vertices[i1].x, pz - surface.vertices[i1].z);
    const double d3 = cross2(surface.vertices[i0].x - surface.vertices[i2].x,
                             surface.vertices[i0].z - surface.vertices[i2].z,
                             px - surface.vertices[i2].x, pz - surface.vertices[i2].z);
    return (d1 > 0 && d2 > 0 && d3 > 0) || (d1 < 0 && d2 < 0 && d3 < 0);
}

bool covered_by_floor(const StructuralWorld& world, std::int16_t sector, double px, double pz) {
    for (const auto& surface : world.surfaces) {
        if (surface.kind != SurfaceKind::Floor || surface.sector != sector) {
            continue;
        }
        for (std::size_t t = 0; t < surface.indices.size() / 3; ++t) {
            if (point_in_triangle_xz(surface, t, px, pz)) {
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
    // Build (X, Y, Z) -> render (X, -Z, Y) with the default 2^-11 scale:
    // one grid square 65536 -> 32 render units, a 16384 storey -> 8.
    const StructuralVertex v = to_render_space(kUnit, -2 * kUnit, 4096);
    CHECK(v.x == 32.0);
    CHECK(v.y == -2.0);
    CHECK(v.z == -64.0);

    // Exact round trip for adversarial int32 values (power-of-two scale).
    const std::int32_t xs[] = {-2147483647 - 1, 2147483647, 0, -1, 65535};
    for (const std::int32_t x : xs) {
        for (const std::int32_t z : xs) {
            const StructuralVertex r = to_render_space(x, 0, z);
            CHECK(static_cast<std::int64_t>(r.x * 2048.0) == static_cast<std::int64_t>(x));
            CHECK(static_cast<std::int64_t>(r.y * 2048.0) ==
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
    CHECK(floor.picnum == 101); // inert source metadata preserved
    REQUIRE(floor.indices.size() == 6);
    CHECK(floor.vertices.size() == 4);

    // Fixture planes: floorz=0, ceilingz=16384 (this M3 fixture is inverted:
    // its ceiling plane sits below its floor plane in render space). Floors
    // must face the ceiling plane and ceilings the floor plane, opposite to
    // each other, so back-face culling works for either convention.
    const double floor_y = -0.0;
    const double ceiling_y = -16384.0 / 2048.0;
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
    const auto [ulo, uhi] = y_extent(*upper);
    CHECK(ulo == -2.0);
    CHECK(uhi == 0.0);
    const auto [llo, lhi] = y_extent(*lower);
    CHECK(llo == -8.0);
    CHECK(lhi == -4.0);

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
    // covered (probe off the internal diagonals).
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

    // Hole interior uncovered; material covered (probe off the diagonals).
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
    CHECK(world.error().detail.find("hole loop vertex not strictly inside") != std::string::npos);
}

// ---------------------------------------------------------------------------
// H. Slope-marked sectors are diagnosed, not interpreted
// ---------------------------------------------------------------------------

TEST_CASE("slope metadata is deferred with notes and flat base Z") {
    const StructuralWorld world = build_fixture("slope_metadata");
    check_triangles_wellformed(world);

    std::size_t slope_notes = 0;
    for (const auto& note : world.notes) {
        if (note.detail.find("slope present") != std::string::npos) {
            ++slope_notes;
            CHECK(note.detail.find("M6 owns slope semantics") != std::string::npos);
            CHECK(note.detail.find("flat base Z") != std::string::npos);
            CHECK(note.record == std::string("sector[0]"));
        }
    }
    CHECK(slope_notes == 2); // floor and ceiling

    // Flat planes at the declared Z; heinum ignored (M3 established the flag,
    // not the magnitude, carries meaning).
    const StructuralSurface* floor = find_surface(world, SurfaceKind::Floor, 0, -1);
    REQUIRE(floor != nullptr);
    for (const auto& v : floor->vertices) {
        CHECK(v.y == 0.0);
    }
    const StructuralSurface* ceiling = find_surface(world, SurfaceKind::Ceiling, 0, -1);
    REQUIRE(ceiling != nullptr);
    for (const auto& v : ceiling->vertices) {
        CHECK(v.y == -16384.0 / 2048.0);
    }
}

TEST_CASE("masked walls are noted as deferred, spans stay plain") {
    const StructuralWorld world = build_fixture("masked_wall");
    check_triangles_wellformed(world);
    std::size_t masked_notes = 0;
    for (const auto& note : world.notes) {
        if (note.record == std::string("wall[1]") || note.record == std::string("wall[7]")) {
            CHECK(note.detail.find("masked wall") != std::string::npos);
            ++masked_notes;
        }
    }
    CHECK(masked_notes == 2);
    // Equal heights in this fixture: the masked portal walls emit no spans at
    // all beyond the notes.
    CHECK(count_kind(world, SurfaceKind::PortalUpper) == 0);
    CHECK(count_kind(world, SurfaceKind::PortalLower) == 0);
}
