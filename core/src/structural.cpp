#include "fauxbuild/structural.hpp"

#include <array>

#include "earcut.hpp"
#include "earcut_adapt.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "fauxbuild/check.hpp"
#include "fauxbuild/map_validate.hpp"

namespace fauxbuild {

// ---------------------------------------------------------------------------
// Exact integer predicates.
//
// Every orientation decision runs on Build-space int32 coordinates widened to
// int64. Products of differences can exceed 64 bits, so accumulators are
// two-limb signed 128-bit values (NUMERICS: 64-bit integers for products and
// cross products; no floating point anywhere in the geometric predicates, so
// results are exact and platform-independent).
// ---------------------------------------------------------------------------

namespace {

struct S128 {
    std::uint64_t lo = 0;
    std::int64_t hi = 0;

    void add(const S128& other) {
        const std::uint64_t old = lo;
        lo += other.lo;
        const std::uint64_t carry = lo < old ? 1u : 0u;
        hi += other.hi + static_cast<std::int64_t>(carry);
    }

    // value += a * b, exact for any int64 a, b.
    void add_product(std::int64_t a, std::int64_t b) {
        const bool negative = (a < 0) != (b < 0);
        const std::uint64_t ua =
            a < 0 ? -static_cast<std::uint64_t>(a) : static_cast<std::uint64_t>(a);
        const std::uint64_t ub =
            b < 0 ? -static_cast<std::uint64_t>(b) : static_cast<std::uint64_t>(b);
        const std::uint64_t a0 = ua & 0xffffffffull;
        const std::uint64_t a1 = ua >> 32;
        const std::uint64_t b0 = ub & 0xffffffffull;
        const std::uint64_t b1 = ub >> 32;
        const std::uint64_t p00 = a0 * b0;
        const std::uint64_t p01 = a0 * b1;
        const std::uint64_t p10 = a1 * b0;
        const std::uint64_t p11 = a1 * b1;
        const std::uint64_t mid = p01 + p10; // may wrap
        std::uint64_t h = p11 + (mid < p01 ? 1ull << 32 : 0ull);
        std::uint64_t l = p00;
        const std::uint64_t t = (l >> 32) + (mid & 0xffffffffull);
        l = (l & 0xffffffffull) | (t << 32);
        h += t >> 32;
        if (negative) {
            sub_u128(h, l);
        } else {
            add_u128(h, l);
        }
    }

    void negate() {
        lo = ~lo + 1u;
        hi = static_cast<std::int64_t>(~static_cast<std::uint64_t>(hi) + (lo == 0u ? 1u : 0u));
    }

    int sign() const {
        if (hi != 0) {
            return hi > 0 ? 1 : -1;
        }
        return lo != 0u ? 1 : 0;
    }

  private:
    void add_u128(std::uint64_t v_hi, std::uint64_t v_lo) {
        const std::uint64_t old = lo;
        lo += v_lo;
        hi += static_cast<std::int64_t>(lo < old ? 1u : 0u) + static_cast<std::int64_t>(v_hi);
    }

    void sub_u128(std::uint64_t v_hi, std::uint64_t v_lo) {
        const std::uint64_t old = lo;
        lo -= v_lo;
        const bool borrowed = old < v_lo;
        hi =
            static_cast<std::int64_t>(static_cast<std::uint64_t>(hi) - v_hi - (borrowed ? 1u : 0u));
    }
};

struct Pt {
    std::int64_t x = 0;
    std::int64_t y = 0;
};

// Sign of the cross product (B-A) x (C-A).
int orient_sign(const Pt& a, const Pt& b, const Pt& c) {
    S128 acc;
    acc.add_product(b.x - a.x, c.y - a.y);
    acc.add_product(-(c.x - a.x), b.y - a.y);
    return acc.sign();
}

// Compare |a| < |b|.
bool magnitude_less(const S128& a, const S128& b) {
    const int sa = a.sign();
    const int sb = b.sign();
    if (sa != sb) {
        return sa < sb;
    }
    if (sa == 0) {
        return false;
    }
    S128 abs_a = a;
    S128 abs_b = b;
    if (sa < 0) {
        abs_a.negate();
        abs_b.negate();
    }
    if (abs_a.hi != abs_b.hi) {
        return abs_a.hi < abs_b.hi;
    }
    return abs_a.lo < abs_b.lo;
}

bool signed_equal(const S128& a, const S128& b) {
    return a.sign() == b.sign() && !magnitude_less(a, b) && !magnitude_less(b, a);
}

// Twice the signed area (shoelace) of a ring, exact.
S128 shoelace2(const std::vector<Pt>& ring) {
    S128 acc;
    const std::size_t n = ring.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Pt& a = ring[i];
        const Pt& b = ring[(i + 1) % n];
        acc.add_product(a.x, b.y);
        acc.add_product(-b.x, a.y);
    }
    return acc;
}

// Twice the signed area of one CCW triangle, exact.
S128 triangle_area2(const Pt& a, const Pt& b, const Pt& c) {
    S128 acc;
    acc.add_product(b.x - a.x, c.y - a.y);
    acc.add_product(-(c.x - a.x), b.y - a.y);
    return acc;
}

enum class PipClass { Outside, Inside, Boundary };

// Point-in-polygon by ray casting with exact predicates: counts ring edges
// crossing the horizontal ray from p towards +X. Touching the boundary
// classifies as Boundary.
PipClass classify_point(const Pt& p, const std::vector<Pt>& ring) {
    bool inside = false;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Pt& a = ring[i];
        const Pt& b = ring[(i + 1) % ring.size()];
        if (a.x == p.x && a.y == p.y) {
            return PipClass::Boundary;
        }
        // Explicit on-segment test first, for every edge. The crossing test
        // below skips edges lying entirely on one side of the ray height, and
        // a horizontal edge AT p.y satisfies that (both endpoints compare
        // equal), so a point resting on a horizontal edge was reported Outside
        // unless it happened to coincide with a vertex. Real content relies on
        // this: E1L1 sector 147 has a hole vertex sitting exactly on a
        // horizontal stretch of its outer boundary, which is valid geometry
        // that earcut triangulates correctly.
        if (orient_sign(a, b, p) == 0 && std::min(a.x, b.x) <= p.x && p.x <= std::max(a.x, b.x) &&
            std::min(a.y, b.y) <= p.y && p.y <= std::max(a.y, b.y)) {
            return PipClass::Boundary;
        }
        if ((a.y > p.y) == (b.y > p.y)) {
            continue; // edge entirely on one side of the ray height
        }
        const int s = orient_sign(a, b, p);
        if (s == 0) {
            // p on the edge's line with height strictly between the endpoints:
            // on the segment, hence on the boundary.
            return PipClass::Boundary;
        }
        const std::int64_t dy = b.y - a.y;
        if ((s > 0) == (dy > 0)) {
            inside = !inside;
        }
    }
    return inside ? PipClass::Inside : PipClass::Outside;
}

// ---------------------------------------------------------------------------
// Sector wall-loop extraction.
// ---------------------------------------------------------------------------

struct WallLoop {
    std::vector<std::size_t> walls; // wall indices in point2 traversal order
    bool ccw = false;               // signed shoelace of stored order
    S128 area2{};                   // signed, in stored order
};

struct SectorLoops {
    std::vector<WallLoop> loops; // discovery order (ascending start wall)
    std::size_t outer = 0;       // index into loops; largest |area|
};

ParseError sector_error(const std::string& source, std::int64_t sector, const std::string& detail) {
    return ParseError{source, 0, "sector[" + std::to_string(sector) + "]",
                      ErrorCode::InvalidTopology, detail};
}

// Extract closed wall loops from a sector's wall range by walking point2
// chains. The map has passed validate_map beforehand, so these are defensive
// fail-closed checks, never repairs: the walk is explicitly bounded and the
// map is never mutated.
Result<SectorLoops> extract_loops(const mapv7::MapData& map, std::size_t s) {
    const mapv7::Sector& sector = map.sectors[s];
    const auto begin = static_cast<std::int64_t>(sector.wallptr);
    const auto count = static_cast<std::int64_t>(sector.wallnum);
    const auto wall_count = static_cast<std::int64_t>(map.walls.size());

    SectorLoops result;
    std::vector<std::uint8_t> visited(map.walls.size(), 0);
    for (std::int64_t start = begin; start < begin + count; ++start) {
        if (visited[static_cast<std::size_t>(start)] != 0) {
            continue;
        }
        WallLoop loop;
        std::int64_t w = start;
        for (std::int64_t steps = 0;; ++steps) {
            if (steps > count) {
                return Result<SectorLoops>::err(
                    sector_error(map.source, static_cast<std::int64_t>(s),
                                 "loop extraction failed: walk from wall " + std::to_string(start) +
                                     " does not close within the sector wall range"));
            }
            if (visited[static_cast<std::size_t>(w)] != 0) {
                if (w == start) {
                    break; // loop closed
                }
                return Result<SectorLoops>::err(sector_error(
                    map.source, static_cast<std::int64_t>(s),
                    "loop extraction failed: wall " + std::to_string(w) + " entered twice"));
            }
            visited[static_cast<std::size_t>(w)] = 1;
            loop.walls.push_back(static_cast<std::size_t>(w));
            const std::int64_t next = map.walls[static_cast<std::size_t>(w)].point2;
            if (next < begin || next >= begin + count || next < 0 || next >= wall_count) {
                return Result<SectorLoops>::err(
                    sector_error(map.source, static_cast<std::int64_t>(s),
                                 "loop extraction failed: point2 of wall " + std::to_string(w) +
                                     " escapes the sector wall range"));
            }
            w = next;
        }
        std::vector<Pt> ring;
        ring.reserve(loop.walls.size());
        for (const std::size_t wall : loop.walls) {
            ring.push_back(Pt{map.walls[wall].x, map.walls[wall].y});
        }
        loop.area2 = shoelace2(ring);
        loop.ccw = loop.area2.sign() > 0;
        result.loops.push_back(std::move(loop));
    }
    for (std::int64_t w = begin; w < begin + count; ++w) {
        if (visited[static_cast<std::size_t>(w)] == 0) {
            return Result<SectorLoops>::err(sector_error(map.source, static_cast<std::int64_t>(s),
                                                         "loop extraction failed: wall " +
                                                             std::to_string(w) +
                                                             " is not part of any closed loop"));
        }
    }
    if (result.loops.empty()) {
        return Result<SectorLoops>::err(
            sector_error(map.source, static_cast<std::int64_t>(s),
                         "loop extraction failed: sector owns no walls"));
    }
    for (std::size_t i = 1; i < result.loops.size(); ++i) {
        if (magnitude_less(result.loops[result.outer].area2, result.loops[i].area2)) {
            result.outer = i;
        }
    }
    return Result<SectorLoops>::ok(std::move(result));
}

// ---------------------------------------------------------------------------
// Triangulation: FauxBuild exact validation -> earcut -> FauxBuild exact
// verification (D0017).
//
// The bespoke ear clipper this replaces was correct where it succeeded but its
// domain was narrower than Build content requires: 33 of 2450 sectors across
// six legally owned maps failed, in five distinct classes, and E1L1 could not
// be built at all. earcut (third_party/earcut, ISC, pinned v3.2.3) is robust on
// that geometry but performs no validation whatsoever — it will happily
// triangulate a self-intersecting bowtie and emit garbage. The old
// implementation conflated validating topology with triangulating it; keeping
// those separate gives both properties.
//
// earcut is a mechanism, never an authority: its output is checked against the
// exact expected area before any surface is emitted.
// ---------------------------------------------------------------------------

// A ring as earcut consumes it. Disposable input representation only; MapData
// is never mutated and these coordinates are exact int64 copies.
using EcRing = std::vector<std::array<std::int64_t, 2>>;

// Do segments ab and cd properly cross? Shared endpoints and collinear touching
// are not crossings: real Build sectors legitimately touch their own hole
// boundaries, and earcut handles those correctly. Only a true transversal
// crossing is invalid.
bool segments_properly_cross(const Pt& a, const Pt& b, const Pt& c, const Pt& d) {
    const int d1 = orient_sign(a, b, c);
    const int d2 = orient_sign(a, b, d);
    const int d3 = orient_sign(c, d, a);
    const int d4 = orient_sign(c, d, b);
    return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}

// Self-intersection over one ring, exact. O(n^2); sector wall counts are small
// (largest observed sector is well under a thousand walls) and this runs once
// per sector, so a sweep line is not justified without profiling evidence.
bool ring_self_intersects(const std::vector<Pt>& ring) {
    const std::size_t n = ring.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Pt& a = ring[i];
        const Pt& b = ring[(i + 1) % n];
        for (std::size_t j = i + 1; j < n; ++j) {
            if (j == i || (j + 1) % n == i || (i + 1) % n == j) {
                continue; // adjacent edges share an endpoint by construction
            }
            if (segments_properly_cross(a, b, ring[j], ring[(j + 1) % n])) {
                return true;
            }
        }
    }
    return false;
}

// Validation owns input validity; earcut never sees anything this rejects.
// Deliberately NOT re-imposing "every hole vertex strictly inside the outer
// loop": real Build geometry shares and touches boundary vertices, and the
// reductions in tests/unit/structural.test.cpp pin that as valid.
Result<void> validate_loops_for_triangulation(const std::string& source, std::int64_t sector,
                                              const std::vector<Pt>& outer,
                                              const std::vector<std::vector<Pt>>& holes) {
    if (outer.size() < 3) {
        return Result<void>::err(sector_error(
            source, sector, "triangulation failed: outer loop has fewer than three vertices"));
    }
    if (ring_self_intersects(outer)) {
        return Result<void>::err(
            sector_error(source, sector, "triangulation failed: outer loop self-intersects"));
    }
    for (std::size_t h = 0; h < holes.size(); ++h) {
        const auto& hole = holes[h];
        if (hole.size() < 3) {
            return Result<void>::err(sector_error(source, sector,
                                                  "triangulation failed: hole loop " +
                                                      std::to_string(h) +
                                                      " has fewer than three "
                                                      "vertices"));
        }
        if (ring_self_intersects(hole)) {
            return Result<void>::err(sector_error(source, sector,
                                                  "triangulation failed: hole loop " +
                                                      std::to_string(h) + " self-intersects"));
        }
        // A hole may touch the outer boundary but may not leave it, and its
        // edges may not cross outer edges transversally.
        bool any_inside = false;
        for (const Pt& p : hole) {
            const PipClass cls = classify_point(p, outer);
            if (cls == PipClass::Outside) {
                return Result<void>::err(sector_error(source, sector,
                                                      "triangulation failed: hole loop " +
                                                          std::to_string(h) +
                                                          " has a vertex outside the outer loop"));
            }
            if (cls == PipClass::Inside) {
                any_inside = true;
            }
        }
        if (!any_inside) {
            return Result<void>::err(sector_error(source, sector,
                                                  "triangulation failed: hole loop " +
                                                      std::to_string(h) +
                                                      " lies entirely on the outer boundary"));
        }
        for (std::size_t i = 0; i < hole.size(); ++i) {
            const Pt& a = hole[i];
            const Pt& b = hole[(i + 1) % hole.size()];
            for (std::size_t j = 0; j < outer.size(); ++j) {
                if (segments_properly_cross(a, b, outer[j], outer[(j + 1) % outer.size()])) {
                    return Result<void>::err(sector_error(source, sector,
                                                          "triangulation failed: hole loop " +
                                                              std::to_string(h) +
                                                              " crosses the outer boundary"));
                }
            }
        }
        for (std::size_t g = h + 1; g < holes.size(); ++g) {
            for (std::size_t i = 0; i < hole.size(); ++i) {
                for (std::size_t j = 0; j < holes[g].size(); ++j) {
                    if (segments_properly_cross(hole[i], hole[(i + 1) % hole.size()], holes[g][j],
                                                holes[g][(j + 1) % holes[g].size()])) {
                        return Result<void>::err(sector_error(source, sector,
                                                              "triangulation failed: hole loops " +
                                                                  std::to_string(h) + " and " +
                                                                  std::to_string(g) + " cross"));
                    }
                }
            }
        }
    }
    return Result<void>::ok();
}

struct Triangulation {
    std::vector<Pt> points;
    std::vector<std::uint32_t> triangles;
    bool degenerate = false; // exact zero area: emit no surface (D0018)
};

Result<Triangulation> triangulate_sector(const mapv7::MapData& map, std::size_t s,
                                         const SectorLoops& loops) {
    const auto sector_index = static_cast<std::int64_t>(s);

    auto ring_of = [&](const WallLoop& loop, bool want_ccw) {
        std::vector<std::size_t> walls = loop.walls;
        if (loop.ccw != want_ccw) {
            std::reverse(walls.begin(), walls.end());
        }
        std::vector<Pt> ring;
        ring.reserve(walls.size());
        for (const std::size_t w : walls) {
            ring.push_back(Pt{map.walls[w].x, map.walls[w].y});
        }
        return ring;
    };

    const std::vector<Pt> outer = ring_of(loops.loops[loops.outer], /*want_ccw=*/true);
    std::vector<std::vector<Pt>> holes;
    for (std::size_t i = 0; i < loops.loops.size(); ++i) {
        if (i != loops.outer) {
            holes.push_back(ring_of(loops.loops[i], /*want_ccw=*/false));
        }
    }

    // Validation runs BEFORE the degenerate check, not after: a self-intersecting
    // bowtie has exactly zero net signed area, so testing for degeneracy first
    // would classify malformed topology as a benign empty surface and return
    // success. Order matters here.
    auto valid = validate_loops_for_triangulation(map.source, sector_index, outer, holes);
    if (!valid.is_ok()) {
        return Result<Triangulation>::err(valid.error());
    }

    // D0018: an exactly zero-area outer loop that survived validation is a
    // degenerate derived surface, not malformed topology — real shipped content
    // contains collinear sectors, and one of them must not cost the whole world.
    const S128 outer_area = shoelace2(outer);
    if (outer_area.sign() == 0) {
        Triangulation empty;
        empty.degenerate = true;
        return Result<Triangulation>::ok(std::move(empty));
    }

    // Exact expected area: outer minus holes.
    S128 expected = outer_area;
    for (const auto& hole : holes) {
        S128 hole_area = shoelace2(hole); // holes are CW, so this is negative
        expected.add(hole_area);
    }
    if (expected.sign() <= 0) {
        return Result<Triangulation>::err(
            sector_error(map.source, sector_index,
                         "triangulation failed: holes consume the whole outer loop area"));
    }

    Triangulation out;
    std::vector<EcRing> rings;
    rings.reserve(1 + holes.size());
    EcRing ec_outer;
    for (const Pt& p : outer) {
        ec_outer.push_back({p.x, p.y});
        out.points.push_back(p);
    }
    rings.push_back(std::move(ec_outer));
    for (const auto& hole : holes) {
        EcRing ec_hole;
        for (const Pt& p : hole) {
            ec_hole.push_back({p.x, p.y});
            out.points.push_back(p);
        }
        rings.push_back(std::move(ec_hole));
    }

    out.triangles = mapbox::earcut<std::uint32_t>(rings);

    // ---- Post-verification. earcut is a mechanism, not an authority. ----
    if (out.triangles.empty() || out.triangles.size() % 3 != 0) {
        return Result<Triangulation>::err(sector_error(
            map.source, sector_index,
            "triangulation failed: triangle list is empty or not a multiple of three"));
    }
    for (const std::uint32_t index : out.triangles) {
        if (static_cast<std::size_t>(index) >= out.points.size()) {
            return Result<Triangulation>::err(sector_error(
                map.source, sector_index, "triangulation failed: triangle index out of range"));
        }
    }
    S128 triangle_sum;
    for (std::size_t t = 0; t + 2 < out.triangles.size(); t += 3) {
        const Pt& a = out.points[out.triangles[t]];
        const Pt& b = out.points[out.triangles[t + 1]];
        const Pt& c = out.points[out.triangles[t + 2]];
        const S128 area = triangle_area2(a, b, c);
        if (area.sign() == 0) {
            return Result<Triangulation>::err(sector_error(
                map.source, sector_index, "triangulation failed: zero-area triangle emitted"));
        }
        triangle_sum.add(area);
    }
    // The exact oracle: emitted triangles must tile precisely the outer loop
    // minus its holes. A filled hole, a dropped ear, or an overlap all fail
    // here regardless of what earcut reported.
    if (!signed_equal(triangle_sum, expected)) {
        return Result<Triangulation>::err(
            sector_error(map.source, sector_index,
                         "triangulation failed: triangles rejected by the exact area invariant"));
    }
    return Result<Triangulation>::ok(std::move(out));
}

// ---------------------------------------------------------------------------
// World assembly.
// ---------------------------------------------------------------------------

std::vector<std::uint32_t> flipped_triangles(const std::vector<std::uint32_t>& indices) {
    std::vector<std::uint32_t> out = indices;
    for (std::size_t i = 0; i + 2 < out.size(); i += 3) {
        std::swap(out[i], out[i + 2]);
    }
    return out;
}

bool scale_is_power_of_two(double scale) {
    if (!(scale > 0.0)) {
        return false;
    }
    if (scale >= 1.0) {
        const std::uint64_t u = static_cast<std::uint64_t>(scale);
        return static_cast<double>(u) == scale && u != 0 && (u & (u - 1)) == 0;
    }
    const double inv = 1.0 / scale;
    const std::uint64_t u = static_cast<std::uint64_t>(inv);
    return static_cast<double>(u) == inv && u != 0 && (u & (u - 1)) == 0;
}

std::string hex16(std::int16_t value) {
    static const char* digits = "0123456789abcdef";
    auto bits = static_cast<std::uint16_t>(value);
    std::string out = "0000";
    for (int i = 3; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = digits[bits & 0xf];
        bits >>= 4;
    }
    return out;
}

} // namespace

const char* surface_kind_name(SurfaceKind kind) {
    switch (kind) {
    case SurfaceKind::Floor:
        return "floor";
    case SurfaceKind::Ceiling:
        return "ceiling";
    case SurfaceKind::SolidWall:
        return "solid_wall";
    case SurfaceKind::PortalUpper:
        return "portal_upper";
    case SurfaceKind::PortalLower:
        return "portal_lower";
    }
    return "unknown";
}

StructuralVertex to_render_space(std::int32_t x, std::int32_t y, std::int32_t z,
                                 const StructuralOptions& options) {
    // Power-of-two scale keeps the mapping exact (D0016): the product of an
    // int32 and 2^-k is exactly representable and reversible. Internal misuse
    // (non-power-of-two scale) is our own bug, not content -> FB_CHECK.
    FB_CHECK(scale_is_power_of_two(options.scale));
    StructuralVertex v;
    v.x = static_cast<double>(x) * options.scale;
    v.y = -static_cast<double>(z) * options.scale;
    v.z = static_cast<double>(y) * options.scale;
    return v;
}

Result<StructuralWorld> build_structural_world(const mapv7::MapData& map,
                                               const StructuralOptions& options) {
    if (!scale_is_power_of_two(options.scale)) {
        return Result<StructuralWorld>::err(
            {"structural", 0, "options", ErrorCode::Unsupported,
             "render scale must be a power of two (D0016); got a non-conforming value"});
    }

    // Precondition: validated topology. Reuse the M3 validator's vocabulary;
    // surface its first error verbatim so real-map failures stay actionable.
    const ValidationReport report = validate_map(map);
    for (const ValidationIssue& issue : report.issues) {
        if (issue.severity == Severity::Error) {
            return Result<StructuralWorld>::err(
                {map.source, 0, issue.record, issue.code,
                 "map failed structural validation: " + issue.detail});
        }
    }

    StructuralWorld world;

    for (std::size_t s = 0; s < map.sectors.size(); ++s) {
        const mapv7::Sector& sector = map.sectors[s];
        const auto begin = static_cast<std::int64_t>(sector.wallptr);
        const auto count = static_cast<std::int64_t>(sector.wallnum);

        auto loops = extract_loops(map, s);
        if (!loops.is_ok()) {
            return Result<StructuralWorld>::err(loops.error());
        }

        // --- Deferred-feature notes (non-fatal) ---------------------------
        if ((sector.floorstat & mapv7::kStatSloped) != 0) {
            world.notes.push_back(
                {"sector[" + std::to_string(s) + "]",
                 "floor slope present (heinum=" + std::to_string(sector.floorheinum) +
                     "); M5 structural preview uses flat base Z; "
                     "M6 owns slope semantics"});
        }
        if ((sector.ceilingstat & mapv7::kStatSloped) != 0) {
            world.notes.push_back(
                {"sector[" + std::to_string(s) + "]",
                 "ceiling slope present (heinum=" + std::to_string(sector.ceilingheinum) +
                     "); M5 structural preview uses flat base Z; "
                     "M6 owns slope semantics"});
        }
        if (sector.ceilingz > sector.floorz) {
            world.notes.push_back({"sector[" + std::to_string(s) + "]",
                                   "ceilingz " + std::to_string(sector.ceilingz) +
                                       " is below floorz " + std::to_string(sector.floorz) +
                                       " (inverted vertical interval); M5 emits the "
                                       "planes at their declared Z and flips winding "
                                       "to keep floors facing ceilings"});
        }

        // --- Floor and ceiling --------------------------------------------
        auto tri = triangulate_sector(map, s, loops.value());
        if (!tri.is_ok()) {
            return Result<StructuralWorld>::err(tri.error());
        }
        const bool floor_reversed = sector.floorz > sector.ceilingz;

        // D0018: a degenerate (exactly zero-area) sector yields no floor or
        // ceiling, but its wall spans are still geometrically well-defined and
        // are emitted below. The world keeps building.
        if (tri.value().degenerate) {
            const std::string record = "sector[" + std::to_string(s) + "]";
            world.diagnostics.push_back({record, "floor", "zero_area"});
            world.diagnostics.push_back({record, "ceiling", "zero_area"});
        } else {

            StructuralSurface floor;
            floor.kind = SurfaceKind::Floor;
            floor.sector = static_cast<std::int16_t>(s);
            floor.wall = -1;
            floor.picnum = sector.floorpicnum;
            floor.vertices.reserve(tri.value().points.size());
            for (const Pt& p : tri.value().points) {
                floor.vertices.push_back(to_render_space(static_cast<std::int32_t>(p.x),
                                                         static_cast<std::int32_t>(p.y),
                                                         sector.floorz, options));
            }
            floor.indices =
                floor_reversed ? flipped_triangles(tri.value().triangles) : tri.value().triangles;
            world.surfaces.push_back(std::move(floor));

            StructuralSurface ceiling;
            ceiling.kind = SurfaceKind::Ceiling;
            ceiling.sector = static_cast<std::int16_t>(s);
            ceiling.wall = -1;
            ceiling.picnum = sector.ceilingpicnum;
            ceiling.vertices.reserve(tri.value().points.size());
            for (const Pt& p : tri.value().points) {
                ceiling.vertices.push_back(to_render_space(static_cast<std::int32_t>(p.x),
                                                           static_cast<std::int32_t>(p.y),
                                                           sector.ceilingz, options));
            }
            ceiling.indices =
                floor_reversed ? tri.value().triangles : flipped_triangles(tri.value().triangles);
            world.surfaces.push_back(std::move(ceiling));
        } // end non-degenerate floor/ceiling emission

        // --- Wall spans ----------------------------------------------------
        // interior-left flag per wall: after orienting outer CCW / holes CW,
        // the sector material is on the left of every boundary direction; the
        // stored loop order may be either rotation, so record per wall.
        std::vector<char> interior_left(static_cast<std::size_t>(count), 0);
        for (std::size_t li = 0; li < loops.value().loops.size(); ++li) {
            const bool left = (li == loops.value().outer) ? loops.value().loops[li].ccw
                                                          : !loops.value().loops[li].ccw;
            for (const std::size_t wall : loops.value().loops[li].walls) {
                interior_left[wall - static_cast<std::size_t>(begin)] = left ? 1 : 0;
            }
        }

        bool saw_uninterpreted_cstat = false;
        std::int64_t first_uninterpreted_wall = -1;
        std::int16_t first_uninterpreted_cstat = 0;

        auto emit_span = [&](SurfaceKind kind, std::int16_t wall_index, const mapv7::Wall& wall,
                             std::int64_t z_a, std::int64_t z_b, bool left) {
            // Solid spans always present the full interval (normalize inverted
            // sectors); portal spans are only emitted for positive extents.
            if (kind != SurfaceKind::SolidWall && z_a >= z_b) {
                return;
            }
            const std::int64_t zt = std::min(z_a, z_b);
            const std::int64_t zb = std::max(z_a, z_b);
            if (zt >= zb) {
                return;
            }
            const mapv7::Wall& target = map.walls[static_cast<std::size_t>(wall.point2)];
            StructuralSurface surface;
            surface.kind = kind;
            surface.sector = static_cast<std::int16_t>(s);
            surface.wall = wall_index;
            surface.picnum = wall.picnum;
            surface.vertices.push_back(
                to_render_space(wall.x, wall.y, static_cast<std::int32_t>(zt), options));
            surface.vertices.push_back(
                to_render_space(target.x, target.y, static_cast<std::int32_t>(zt), options));
            surface.vertices.push_back(
                to_render_space(target.x, target.y, static_cast<std::int32_t>(zb), options));
            surface.vertices.push_back(
                to_render_space(wall.x, wall.y, static_cast<std::int32_t>(zb), options));
            if (left) {
                surface.indices = {0, 2, 1, 0, 3, 2};
            } else {
                surface.indices = {0, 1, 2, 0, 2, 3};
            }
            world.surfaces.push_back(std::move(surface));
        };

        for (std::int64_t wi = begin; wi < begin + count; ++wi) {
            const mapv7::Wall& wall = map.walls[static_cast<std::size_t>(wi)];
            const bool left = interior_left[static_cast<std::size_t>(wi - begin)] != 0;

            if (wall.nextsector == mapv7::kNoIndex) {
                emit_span(SurfaceKind::SolidWall, static_cast<std::int16_t>(wi), wall,
                          sector.ceilingz, sector.floorz, left);
            } else {
                // Structural portal spans: only the parts of the own wall
                // outside the vertical opening shared with the neighbour
                // (RENDERING_CONTRACT: never a full quad across the opening).
                const mapv7::Sector& neighbour =
                    map.sectors[static_cast<std::size_t>(wall.nextsector)];
                const std::int64_t opening_top =
                    std::max<std::int64_t>(sector.ceilingz, neighbour.ceilingz);
                const std::int64_t opening_bottom =
                    std::min<std::int64_t>(sector.floorz, neighbour.floorz);
                emit_span(SurfaceKind::PortalUpper, static_cast<std::int16_t>(wi), wall,
                          sector.ceilingz, opening_top, left);
                emit_span(SurfaceKind::PortalLower, static_cast<std::int16_t>(wi), wall,
                          opening_bottom, sector.floorz, left);
            }

            if ((wall.cstat & mapv7::kWallCstatMasked) != 0) {
                world.notes.push_back({"wall[" + std::to_string(wi) + "]",
                                       "masked wall (cstat 0x0010, overpicnum " +
                                           std::to_string(wall.overpicnum) +
                                           "); M5 emits plain structural spans only; M6 owns "
                                           "masked/one-way wall semantics"});
            } else if (wall.cstat != 0) {
                if (!saw_uninterpreted_cstat) {
                    saw_uninterpreted_cstat = true;
                    first_uninterpreted_wall = wi;
                    first_uninterpreted_cstat = wall.cstat;
                }
            }
        }
        if (saw_uninterpreted_cstat) {
            world.notes.push_back(
                {"sector[" + std::to_string(s) + "]",
                 "walls carry uninterpreted cstat flags (first at wall[" +
                     std::to_string(first_uninterpreted_wall) + "], cstat 0x" +
                     hex16(first_uninterpreted_cstat) +
                     "); M5 renders plain structural spans; M6 owns flag semantics"});
        }
    }

    return Result<StructuralWorld>::ok(std::move(world));
}

} // namespace fauxbuild
