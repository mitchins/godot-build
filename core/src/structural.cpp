#include "fauxbuild/structural.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include "earcut_adapt.hpp" // wraps earcut.hpp; see that header

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
        const std::uint64_t ua = a < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(a)
                                       : static_cast<std::uint64_t>(a);
        const std::uint64_t ub = b < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(b)
                                       : static_cast<std::uint64_t>(b);
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
// |a| < |b|. Sign is irrelevant to magnitude: an earlier version short-circuited
// on differing signs and returned `sa < sb`, which compares signs instead. That
// is not an abstract helper bug -- extract_loops picks the outer loop by largest
// |area|, so a sector whose outer loop is wound CW (negative shoelace) against a
// hole wound CCW (positive) selected the *hole* as the outer boundary, and the
// build then failed with the real outer vertices "outside" it. Sign equality is
// signed_equal's job, not this function's.
bool magnitude_less(const S128& a, const S128& b) {
    S128 abs_a = a;
    S128 abs_b = b;
    if (abs_a.sign() < 0) {
        abs_a.negate();
    }
    if (abs_b.sign() < 0) {
        abs_b.negate();
    }
    const auto hi_a = static_cast<std::uint64_t>(abs_a.hi);
    const auto hi_b = static_cast<std::uint64_t>(abs_b.hi);
    if (hi_a != hi_b) {
        return hi_a < hi_b;
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
// earcut computes in double internally. Handing it int64 pushes the narrowing
// conversion inside the standard library, where MSVC reports C4244 at a header
// no external-include rule covers. Converting here instead makes the boundary
// explicit and provably exact: every coordinate originates as a Build int32
// (mapv7::Wall::x/y), and every int32 is exactly representable in a double.
// Validation and the area oracle either side of earcut remain exact integer
// arithmetic -- only the ring handed to the library is double.
using EcRing = std::vector<std::array<double, 2>>;

double exact_as_double(std::int64_t v) {
    FB_CHECK(v >= INT32_MIN && v <= INT32_MAX); // exactness precondition
    return static_cast<double>(v);
}

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
        ec_outer.push_back({exact_as_double(p.x), exact_as_double(p.y)});
        out.points.push_back(p);
    }
    rings.push_back(std::move(ec_outer));
    for (const auto& hole : holes) {
        EcRing ec_hole;
        for (const Pt& p : hole) {
            ec_hole.push_back({exact_as_double(p.x), exact_as_double(p.y)});
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

// D0016 requires the render scale to be a positive, finite power of two, so
// every Build int32 coordinate maps to an exactly representable double and back.
//
// Expressed with frexp rather than integer casts. An earlier version cast the
// scale (and 1/scale) to uint64 to test it, which is undefined behaviour when
// the value does not fit: UBSan-confirmed on a very large finite scale and,
// via 1/scale, on a very small positive one. frexp states the contract
// directly -- a power of two is exactly 0.5 x 2^exp -- and needs no conversion,
// so infinities, NaN and denormals are ordinary inputs rather than hazards.
bool scale_is_power_of_two(double scale) {
    if (!std::isfinite(scale) || !(scale > 0.0)) {
        return false; // NaN fails the ordered comparison; so do zero and negatives
    }
    int exponent = 0;
    const double mantissa = std::frexp(scale, &exponent);
    // frexp yields mantissa in [0.5, 1); it is exactly 0.5 only for a power of
    // two.
    if (mantissa != 0.5) {
        return false;
    }
    // Being a power of two is necessary but not sufficient. D0016's actual
    // requirement is that every int32 maps to an exactly representable double
    // and back, and an extreme exponent breaks that even though the value is a
    // power of two: at 2^-1074 the products underflow into denormals and the
    // round trip stops being exact. Test the contract itself at the int32
    // extremes rather than reasoning about exponent bounds.
    const double inverse = 1.0 / scale;
    if (!std::isfinite(inverse)) {
        return false;
    }
    for (const std::int32_t probe : {std::numeric_limits<std::int32_t>::min(), std::int32_t{-1},
                                     std::int32_t{1}, std::numeric_limits<std::int32_t>::max()}) {
        const double scaled = static_cast<double>(probe) * scale;
        if (!std::isfinite(scaled) || scaled * inverse != static_cast<double>(probe)) {
            return false;
        }
    }
    return true;
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

bool render_z_is_exact(std::int64_t z) {
    constexpr std::int64_t kExactIntegerDouble = 1LL << 53;
    return z >= -kExactIntegerDouble && z <= kExactIntegerDouble;
}

StructuralVertex to_render_space(std::int32_t x, std::int32_t y, std::int64_t z,
                                 const StructuralOptions& options) {
    // Power-of-two scale keeps the mapping exact (D0016): the product of an
    // int32 and 2^-k is exactly representable and reversible. Internal misuse
    // (non-power-of-two scale) is our own bug, not content -> FB_CHECK.
    FB_CHECK(scale_is_power_of_two(options.scale));
    // Vertical scale is the horizontal one divided by the format's 16:1 unit
    // ratio (D0016 amendment). 16 is a power of two, so the quotient is too
    // and the exactness/reversibility contract is unchanged.
    const double vertical_scale = options.scale / kBuildVerticalUnitsPerHorizontal;
    FB_CHECK(scale_is_power_of_two(vertical_scale));
    // Derived Z must not be silently narrowed on its way to render space.
    FB_CHECK(render_z_is_exact(z));
    StructuralVertex v;
    v.x = static_cast<double>(x) * options.scale;
    v.y = -static_cast<double>(z) * vertical_scale;
    v.z = static_cast<double>(y) * options.scale;
    return v;
}

// --- slope evaluator (single authority) ------------------------------------
// `heinum` may appear NOWHERE else in this file; ci/check_layering.py pins it.

namespace {

// Exact 128-bit -> binary64. All-binary64 arithmetic, so the result is
// reproducible on every supported toolchain.
//
// An earlier version rejected operands above 2^53 on the theory that they had
// to be exactly representable. That was over-strict: only the RELATIVE
// precision of the quotient matters, and binary64 delivers ~2^-53 relative
// whatever the operand magnitude. The bound that actually matters is on the
// RESULT, and from int32 MAP coordinates the largest reachable |z| is about
// 2^39.5 -- far inside the exactly-representable integer range.
double s128_to_double(const S128& v) {
    constexpr double kTwo64 = 18446744073709551616.0; // 2^64, exact
    // Convert the MAGNITUDE and reapply the sign. Converting the two's
    // complement limbs directly is catastrophically lossy for small negative
    // values: -2000000 is stored as hi = -1, lo = 2^64 - 2000000, and lo
    // loses its low bits to rounding at that magnitude, so the two limbs
    // very nearly cancel and the small true value is destroyed.
    S128 magnitude = v;
    const bool negative = magnitude.sign() < 0;
    if (negative) {
        magnitude.negate();
    }
    const double value =
        static_cast<double>(magnitude.hi) * kTwo64 + static_cast<double>(magnitude.lo);
    return negative ? -value : value;
}

// Flagged for slope with a nonzero heinum, irrespective of hinge usability.
bool slope_flagged(const fauxbuild::mapv7::MapData& map, std::int16_t sector_index,
                   fauxbuild::SurfacePlane plane) {
    const auto si = static_cast<std::size_t>(sector_index);
    if (sector_index < 0 || si >= map.sectors.size()) {
        return false;
    }
    const fauxbuild::mapv7::Sector& sector = map.sectors[si];
    const bool ceiling = plane == fauxbuild::SurfacePlane::Ceiling;
    const std::int16_t stat = ceiling ? sector.ceilingstat : sector.floorstat;
    const std::int16_t heinum = ceiling ? sector.ceilingheinum : sector.floorheinum;
    return (stat & fauxbuild::mapv7::kStatSloped) != 0 && heinum != 0;
}

struct SlopeInputs {
    std::int32_t base_z = 0;
    std::int16_t heinum = 0;
    std::int64_t ax = 0, ay = 0, dx = 0, dy = 0;
    bool sloped = false;
};

SlopeInputs slope_inputs(const fauxbuild::mapv7::MapData& map, std::int16_t sector_index,
                         fauxbuild::SurfacePlane plane) {
    SlopeInputs in;
    const auto si = static_cast<std::size_t>(sector_index);
    if (sector_index < 0 || si >= map.sectors.size()) {
        return in;
    }
    const fauxbuild::mapv7::Sector& sector = map.sectors[si];
    const bool ceiling = plane == fauxbuild::SurfacePlane::Ceiling;
    in.base_z = ceiling ? sector.ceilingz : sector.floorz;
    const std::int16_t stat = ceiling ? sector.ceilingstat : sector.floorstat;
    in.heinum = ceiling ? sector.ceilingheinum : sector.floorheinum;

    // Geometry honours the flag, never the heinum alone.
    if ((stat & fauxbuild::mapv7::kStatSloped) == 0 || in.heinum == 0) {
        return in;
    }
    if (sector.wallnum < 2 || sector.wallptr < 0) {
        return in; // no hinge to tilt about
    }
    const auto first = static_cast<std::size_t>(sector.wallptr);
    if (first >= map.walls.size()) {
        return in;
    }
    const auto second = static_cast<std::size_t>(map.walls[first].point2);
    if (second >= map.walls.size()) {
        return in;
    }
    in.ax = map.walls[first].x;
    in.ay = map.walls[first].y;
    in.dx = static_cast<std::int64_t>(map.walls[second].x) - in.ax;
    in.dy = static_cast<std::int64_t>(map.walls[second].y) - in.ay;
    if (in.dx == 0 && in.dy == 0) {
        return in; // degenerate hinge
    }
    in.sloped = true;
    return in;
}

} // namespace

bool slope_is_evaluable(const fauxbuild::mapv7::MapData& map, std::int16_t sector_index,
                        fauxbuild::SurfacePlane plane) {
    const SlopeInputs in = slope_inputs(map, sector_index, plane);
    if (in.sloped) {
        return true; // hinge present and non-degenerate
    }
    // `sloped` is false either because the plane carries no slope (nothing to
    // evaluate; base Z is exact and correct) or because a FLAGGED plane has no
    // usable hinge. Only the latter is a failure.
    return !slope_flagged(map, sector_index, plane);
}

std::int64_t surface_z_at(const fauxbuild::mapv7::MapData& map, std::int16_t sector_index,
                          fauxbuild::SurfacePlane plane, std::int32_t x, std::int32_t y) {
    const SlopeInputs in = slope_inputs(map, sector_index, plane);
    if (!in.sloped) {
        return in.base_z;
    }

    // Exact: signed cross product of the hinge direction with (P - A), and the
    // squared hinge length.
    S128 cross;
    cross.add_product(in.dx, static_cast<std::int64_t>(y) - in.ay);
    cross.add_product(-in.dy, static_cast<std::int64_t>(x) - in.ax);
    S128 length_squared;
    length_squared.add_product(in.dx, in.dx);
    length_squared.add_product(in.dy, in.dy);

    if (cross.sign() == 0) {
        return in.base_z; // exactly on the hinge line: invariant, any heinum
    }

    // The inexact steps, all binary64. See the header for exactly what is and
    // is not being claimed about reproducibility.
    const double perpendicular = s128_to_double(cross) / std::sqrt(s128_to_double(length_squared));
    const double delta = perpendicular * static_cast<double>(in.heinum) / 256.0;
    if (!std::isfinite(delta)) {
        return in.base_z;
    }

    // Symmetric rounding (half away from zero): this is what makes negating
    // the heinum negate the result exactly, which the ramp fixtures pin.
    const double magnitude = std::floor(std::fabs(delta) + 0.5);
    const std::int64_t rounded =
        delta < 0.0 ? -static_cast<std::int64_t>(magnitude) : static_cast<std::int64_t>(magnitude);
    return static_cast<std::int64_t>(in.base_z) + rounded;
}

// --- end slope evaluator ---------------------------------------------------

Result<StructuralWorld> build_structural_world(const mapv7::MapData& map,
                                               const StructuralOptions& options) {
    if (!scale_is_power_of_two(options.scale)) {
        return Result<StructuralWorld>::err(
            {"structural", 0, "options", ErrorCode::Unsupported,
             "render scale must be a power of two (D0016); got a non-conforming value"});
    }
    // The DERIVED vertical scale must satisfy the same contract. It is not
    // implied by the horizontal one: scale = 2^-1020 is a conforming power of
    // two whose sixteenth is subnormal, and the exactness/reversibility
    // property does not survive that. This is a caller's option value, so it
    // is an external contract violation (structured error), never an
    // FB_CHECK — the assertion in to_render_space guards our own bugs only.
    if (!scale_is_power_of_two(options.scale / kBuildVerticalUnitsPerHorizontal)) {
        return Result<StructuralWorld>::err(
            {"structural", 0, "options", ErrorCode::Unsupported,
             "render scale is too small: the derived vertical scale (scale / 16, D0016 "
             "amendment) is not an exactly reversible power of two"});
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

    // Raw sector-level appearance, filled BEFORE the emission loop so the
    // table is total: a sector that emits no surface (degenerate floor plane,
    // say) still occupies its own index, and `surface.sector` indexes this
    // vector directly. Verbatim copy; nothing here is interpreted (M10 owns
    // visibility's behaviour).
    world.sector_appearance.reserve(map.sectors.size());
    for (const mapv7::Sector& sector : map.sectors) {
        StructuralSectorAppearance appearance;
        appearance.visibility = sector.visibility;
        world.sector_appearance.push_back(appearance);
    }

    for (std::size_t s = 0; s < map.sectors.size(); ++s) {
        const mapv7::Sector& sector = map.sectors[s];
        const auto begin = static_cast<std::int64_t>(sector.wallptr);
        const auto count = static_cast<std::int64_t>(sector.wallnum);

        auto loops = extract_loops(map, s);
        if (!loops.is_ok()) {
            return Result<StructuralWorld>::err(loops.error());
        }

        // --- Slope evaluability (D0019) -----------------------------------
        // A flagged plane whose first-wall hinge has zero length has no
        // defined slope plane. It is never flattened as a substitute, and it
        // never aborts the rest of an otherwise valid world. What is omitted
        // is exactly the geometry whose PLACEMENT depends on that undefined
        // plane; everything independently derivable is retained. Endpoints are
        // never fabricated from base Z.
        const bool floor_plane_defined =
            slope_is_evaluable(map, static_cast<std::int16_t>(s), SurfacePlane::Floor);
        const bool ceiling_plane_defined =
            slope_is_evaluable(map, static_cast<std::int16_t>(s), SurfacePlane::Ceiling);
        if (!floor_plane_defined) {
            world.diagnostics.push_back(
                {"sector[" + std::to_string(s) + "]", "floor", "slope_hinge_degenerate"});
        }
        if (!ceiling_plane_defined) {
            world.diagnostics.push_back(
                {"sector[" + std::to_string(s) + "]", "ceiling", "slope_hinge_degenerate"});
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

            if (floor_plane_defined) {

                StructuralSurface floor;
                floor.kind = SurfaceKind::Floor;
                floor.sector = static_cast<std::int16_t>(s);
                floor.wall = -1;
                floor.appearance.picnum = sector.floorpicnum;
                floor.appearance.raw_stat = sector.floorstat;
                floor.appearance.shade = sector.floorshade;
                floor.appearance.pal = sector.floorpal;
                floor.appearance.xpanning = sector.floorxpanning;
                floor.appearance.ypanning = sector.floorypanning;
                floor.vertices.reserve(tri.value().points.size());
                for (const Pt& p : tri.value().points) {
                    const std::int64_t z = surface_z_at(
                        map, static_cast<std::int16_t>(s), SurfacePlane::Floor,
                        static_cast<std::int32_t>(p.x), static_cast<std::int32_t>(p.y));
                    floor.vertices.push_back(to_render_space(static_cast<std::int32_t>(p.x),
                                                             static_cast<std::int32_t>(p.y), z,
                                                             options));
                }
                floor.indices = floor_reversed ? flipped_triangles(tri.value().triangles)
                                               : tri.value().triangles;
                world.surfaces.push_back(std::move(floor));
            }

            if (ceiling_plane_defined) {

                StructuralSurface ceiling;
                ceiling.kind = SurfaceKind::Ceiling;
                ceiling.sector = static_cast<std::int16_t>(s);
                ceiling.wall = -1;
                ceiling.appearance.picnum = sector.ceilingpicnum;
                ceiling.appearance.raw_stat = sector.ceilingstat;
                ceiling.appearance.shade = sector.ceilingshade;
                ceiling.appearance.pal = sector.ceilingpal;
                ceiling.appearance.xpanning = sector.ceilingxpanning;
                ceiling.appearance.ypanning = sector.ceilingypanning;
                ceiling.vertices.reserve(tri.value().points.size());
                for (const Pt& p : tri.value().points) {
                    const std::int64_t z = surface_z_at(
                        map, static_cast<std::int16_t>(s), SurfacePlane::Ceiling,
                        static_cast<std::int32_t>(p.x), static_cast<std::int32_t>(p.y));
                    ceiling.vertices.push_back(to_render_space(static_cast<std::int32_t>(p.x),
                                                               static_cast<std::int32_t>(p.y), z,
                                                               options));
                }
                ceiling.indices = floor_reversed ? tri.value().triangles
                                                 : flipped_triangles(tri.value().triangles);
                world.surfaces.push_back(std::move(ceiling));
            }
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

        // Whether a span is emitted at all stays a decision about the FLAT
        // interval, exactly as before the evaluator existed. Slope moves
        // vertices; it must not silently change how many surfaces a world has
        // (M6.1 gate G pins E1L1's topology across this change). A later slice
        // owns any refinement of that rule.
        auto emit_span = [&](SurfaceKind kind, std::int16_t wall_index, const mapv7::Wall& wall,
                             std::int64_t z_a, std::int64_t z_b, std::int64_t za_top,
                             std::int64_t za_bottom, std::int64_t zb_top, std::int64_t zb_bottom,
                             bool left) {
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
            // Per-endpoint heights from THE evaluator, so a wall meets a sloped
            // floor or ceiling exactly at the shared XY.
            const std::int64_t a_lo = std::min(za_top, za_bottom);
            const std::int64_t a_hi = std::max(za_top, za_bottom);
            const std::int64_t b_lo = std::min(zb_top, zb_bottom);
            const std::int64_t b_hi = std::max(zb_top, zb_bottom);
            const mapv7::Wall& target = map.walls[static_cast<std::size_t>(wall.point2)];
            StructuralSurface surface;
            surface.kind = kind;
            surface.sector = static_cast<std::int16_t>(s);
            surface.wall = wall_index;
            surface.appearance.picnum = wall.picnum;
            surface.appearance.overpicnum = wall.overpicnum;
            surface.appearance.raw_stat = wall.cstat;
            surface.appearance.shade = wall.shade;
            surface.appearance.pal = wall.pal;
            surface.appearance.xrepeat = wall.xrepeat;
            surface.appearance.yrepeat = wall.yrepeat;
            surface.appearance.xpanning = wall.xpanning;
            surface.appearance.ypanning = wall.ypanning;
            // A sloped plane can close the span at one end. The derived shape
            // is then a triangular WEDGE, not a quad with a zero-area triangle
            // stapled to it -- and a wedge is perfectly good geometry that must
            // not be discarded just because one endpoint closes.
            //
            //   open at A and B       -> quad, two triangles
            //   closed at exactly one -> wedge, one triangle
            //   closed at both        -> nothing to emit
            //
            // Winding follows the quad each case degenerates from, so `left`
            // keeps meaning exactly what it did before.
            const bool open_a = a_lo != a_hi;
            const bool open_b = b_lo != b_hi;
            if (!open_a && !open_b) {
                return; // the span closes along its whole length
            }
            surface.vertices.push_back(to_render_space(wall.x, wall.y, a_lo, options));
            surface.vertices.push_back(to_render_space(target.x, target.y, b_lo, options));
            if (open_a && open_b) {
                surface.vertices.push_back(to_render_space(target.x, target.y, b_hi, options));
                surface.vertices.push_back(to_render_space(wall.x, wall.y, a_hi, options));
                surface.indices = left ? std::vector<std::uint32_t>{0, 2, 1, 0, 3, 2}
                                       : std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3};
            } else {
                // Closed at A: its two vertices coincide, so the quad's second
                // face vanishes and the first survives. Closed at B: the
                // reverse. Either way one triangle remains.
                surface.vertices.push_back(
                    open_a ? to_render_space(wall.x, wall.y, a_hi, options)
                           : to_render_space(target.x, target.y, b_hi, options));
                surface.indices = left ? std::vector<std::uint32_t>{0, 2, 1}
                                       : std::vector<std::uint32_t>{0, 1, 2};
            }
            world.surfaces.push_back(std::move(surface));
        };

        for (std::int64_t wi = begin; wi < begin + count; ++wi) {
            const mapv7::Wall& wall = map.walls[static_cast<std::size_t>(wi)];
            const bool left = interior_left[static_cast<std::size_t>(wi - begin)] != 0;

            const mapv7::Wall& far = map.walls[static_cast<std::size_t>(wall.point2)];
            const auto si = static_cast<std::int16_t>(s);
            // Both endpoints, both planes, through the one evaluator.
            const std::int64_t ceil_a =
                surface_z_at(map, si, SurfacePlane::Ceiling, wall.x, wall.y);
            const std::int64_t ceil_b = surface_z_at(map, si, SurfacePlane::Ceiling, far.x, far.y);
            const std::int64_t floor_a = surface_z_at(map, si, SurfacePlane::Floor, wall.x, wall.y);
            const std::int64_t floor_b = surface_z_at(map, si, SurfacePlane::Floor, far.x, far.y);

            if (wall.nextsector == mapv7::kNoIndex) {
                // A solid span needs BOTH of this sector's planes for its
                // vertical endpoints (D0019).
                if (floor_plane_defined && ceiling_plane_defined) {
                    emit_span(SurfaceKind::SolidWall, static_cast<std::int16_t>(wi), wall,
                              sector.ceilingz, sector.floorz, ceil_a, floor_a, ceil_b, floor_b,
                              left);
                }
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
                // The neighbour's planes are evaluated at the SAME XY, so a
                // portal edge meets the adjoining sloped surface exactly.
                const auto ni = static_cast<std::int16_t>(wall.nextsector);
                const std::int64_t n_ceil_a =
                    surface_z_at(map, ni, SurfacePlane::Ceiling, wall.x, wall.y);
                const std::int64_t n_ceil_b =
                    surface_z_at(map, ni, SurfacePlane::Ceiling, far.x, far.y);
                const std::int64_t n_floor_a =
                    surface_z_at(map, ni, SurfacePlane::Floor, wall.x, wall.y);
                const std::int64_t n_floor_b =
                    surface_z_at(map, ni, SurfacePlane::Floor, far.x, far.y);
                // Each portal span depends only on the CEILING planes or only
                // on the FLOOR planes, of this sector and its neighbour. An
                // undefined plane removes exactly the spans that need it
                // (D0019); the others remain independently derivable.
                const bool n_ceiling_defined = slope_is_evaluable(map, ni, SurfacePlane::Ceiling);
                const bool n_floor_defined = slope_is_evaluable(map, ni, SurfacePlane::Floor);
                if (ceiling_plane_defined && n_ceiling_defined) {
                    emit_span(SurfaceKind::PortalUpper, static_cast<std::int16_t>(wi), wall,
                              sector.ceilingz, opening_top, ceil_a, std::max(ceil_a, n_ceil_a),
                              ceil_b, std::max(ceil_b, n_ceil_b), left);
                }
                if (floor_plane_defined && n_floor_defined) {
                    emit_span(SurfaceKind::PortalLower, static_cast<std::int16_t>(wi), wall,
                              opening_bottom, sector.floorz, std::min(floor_a, n_floor_a), floor_a,
                              std::min(floor_b, n_floor_b), floor_b, left);
                }
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
