#include "fauxbuild/structural.hpp"

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

// Strictly-inside test for a CCW triangle (a, b, c). Points on an edge are not
// strictly inside (orientation zero), which is what ear clipping needs.
bool strictly_inside_triangle(const Pt& a, const Pt& b, const Pt& c, const Pt& p) {
    return orient_sign(a, b, p) > 0 && orient_sign(b, c, p) > 0 && orient_sign(c, a, p) > 0;
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
// Triangulation: ear clipping with hole bridging. Original implementation of
// the standard published algorithm class (PROVENANCE row 12). Holes are spliced
// into the outer ring through a bridge at the hole's rightmost vertex; the
// merged ring is then ear-clipped with exact integer predicates. Two exact
// area invariants verify the construction on every sector (bridging and
// clipping both preserve signed area), so an algorithm failure on any content
// surfaces as a structured error instead of wrong geometry.
// ---------------------------------------------------------------------------

// Bridge one hole into the merged ring. Obstacles are the merged ring's edges
// plus the rings of holes not yet bridged (full-geometry visibility, so
// processing order cannot let a bridge cross a future hole). Among visible
// targets, vertices that are not already bridge pinches are preferred: a
// fresh target keeps every pinch at degree two, which ear clipping handles;
// stacking two bridges on one outer vertex creates a degree-three pinch the
// conservative ear rules cannot resolve (found on the double_hole fixture).
Result<void> bridge_hole(const std::string& source, std::int64_t sector, std::vector<Pt>& merged,
                         std::vector<std::uint8_t>& is_pinch, const std::vector<Pt>& hole,
                         const std::vector<std::vector<Pt>>& later_holes) {
    // M: rightmost hole vertex; ties prefer the topmost.
    std::size_t m = 0;
    for (std::size_t i = 1; i < hole.size(); ++i) {
        if (hole[i].x > hole[m].x || (hole[i].x == hole[m].x && hole[i].y > hole[m].y)) {
            m = i;
        }
    }
    const Pt mp = hole[m];

    // Does edge a->b block the candidate bridge M->V?
    auto blocked_by_edge = [&](const Pt& v, const Pt& a, const Pt& b) {
        const int o1 = orient_sign(mp, v, a);
        const int o2 = orient_sign(mp, v, b);
        const int o3 = orient_sign(a, b, mp);
        const int o4 = orient_sign(a, b, v);
        if (o1 * o2 < 0 && o3 * o4 < 0) {
            return true; // proper crossing
        }
        if (o1 == 0 && o2 == 0) {
            // Collinear with the bridge line: a positive-length interval
            // overlap on the dominant axis blocks.
            const bool use_x = std::abs(v.x - mp.x) >= std::abs(v.y - mp.y);
            if (use_x) {
                return std::max(mp.x, v.x) > std::min(a.x, b.x) &&
                       std::max(a.x, b.x) > std::min(mp.x, v.x);
            }
            return std::max(mp.y, v.y) > std::min(a.y, b.y) &&
                   std::max(a.y, b.y) > std::min(mp.y, v.y);
        }
        // A vertex strictly inside the bridge segment would pinch the boundary
        // through a third point; such bridges are not simple.
        auto strictly_between = [&](const Pt& q) {
            return (q.x > std::min(mp.x, v.x) && q.x < std::max(mp.x, v.x)) ||
                   (q.y > std::min(mp.y, v.y) && q.y < std::max(mp.y, v.y));
        };
        if (o1 == 0 && strictly_between(a)) {
            return true;
        }
        if (o2 == 0 && strictly_between(b)) {
            return true;
        }
        return false;
    };

    auto visible = [&](std::size_t i) {
        const Pt& v = merged[i];
        for (std::size_t j = 0; j < merged.size(); ++j) {
            const Pt& a = merged[j];
            const Pt& b = merged[(j + 1) % merged.size()];
            const bool incident = (j == i) || ((j + 1) % merged.size() == i);
            if (incident) {
                continue; // edges sharing V may touch the bridge at V
            }
            if (blocked_by_edge(v, a, b)) {
                return false;
            }
        }
        for (const std::vector<Pt>& ring : later_holes) {
            for (std::size_t j = 0; j < ring.size(); ++j) {
                if (blocked_by_edge(v, ring[j], ring[(j + 1) % ring.size()])) {
                    return false;
                }
            }
        }
        return true;
    };

    // R = visible merged vertex with x >= M.x at the minimum polar angle from
    // +X (exact cross-product comparison; ties resolved by vertex order),
    // preferring targets that are not already bridge pinches.
    std::size_t best = merged.size();
    for (const bool want_fresh : {true, false}) {
        for (std::size_t i = 0; i < merged.size(); ++i) {
            const Pt& v = merged[i];
            if (v.x < mp.x || (v.x == mp.x && v.y == mp.y)) {
                continue;
            }
            if (want_fresh && is_pinch[i] != 0) {
                continue;
            }
            if (!visible(i)) {
                continue;
            }
            if (best == merged.size()) {
                best = i;
                continue;
            }
            const Pt da{v.x - mp.x, v.y - mp.y};
            const Pt db{merged[best].x - mp.x, merged[best].y - mp.y};
            S128 cr;
            cr.add_product(da.x, db.y);
            cr.add_product(-db.x, da.y);
            if (cr.sign() > 0 || (cr.sign() == 0 && da.y < db.y)) {
                best = i; // best is strictly CCW of v: v has the smaller angle
            }
        }
        if (best != merged.size()) {
            break; // found a fresh target; pinch targets are the fallback
        }
    }
    if (best == merged.size()) {
        return Result<void>::err(sector_error(
            source, sector, "triangulation failed: no visible bridge target for hole loop"));
    }

    // Splice the full hole cycle between R and a duplicate of R. The hole ring
    // arrives CW (normalized): walking its successors from M keeps the sector
    // material on the left, matching the CCW merged ring. Both M and R appear
    // twice (the standard pinch construction).
    const std::size_t r = best;
    const std::size_t hole_n = hole.size();
    std::vector<Pt> chain;
    std::vector<std::uint8_t> chain_pinch;
    chain.reserve(hole_n + 2);
    chain_pinch.reserve(hole_n + 2);
    chain.push_back(mp);
    chain_pinch.push_back(1); // M duplicate arrives pinched
    for (std::size_t step = 1; step < hole_n; ++step) {
        chain.push_back(hole[(m + step) % hole_n]);
        chain_pinch.push_back(0);
    }
    chain.push_back(mp);
    chain_pinch.push_back(1);
    chain.push_back(merged[r]);
    chain_pinch.push_back(1); // the R duplicate is born pinched
    is_pinch[r] = 1;
    merged.insert(merged.begin() + static_cast<std::ptrdiff_t>(r) + 1, chain.begin(), chain.end());
    is_pinch.insert(is_pinch.begin() + static_cast<std::ptrdiff_t>(r) + 1, chain_pinch.begin(),
                    chain_pinch.end());
    return Result<void>::ok();
}

// Ear-clip a simple CCW ring. Collinear vertices are removed silently (no
// zero-area output); reflex vertices are skipped; an ear is a convex vertex
// with no other live vertex strictly inside its triangle. Fails closed with a
// structured error when no progress is possible.
Result<std::vector<std::uint32_t>> ear_clip(const std::string& source, std::int64_t sector,
                                            const std::vector<Pt>& ring) {
    const std::size_t n = ring.size();
    std::vector<std::uint32_t> prev(n), next(n);
    for (std::size_t i = 0; i < n; ++i) {
        prev[i] = static_cast<std::uint32_t>((i + n - 1) % n);
        next[i] = static_cast<std::uint32_t>((i + 1) % n);
    }
    std::size_t count = n;
    std::vector<std::uint32_t> triangles;
    triangles.reserve((n - 2) * 3);

    auto remove = [&](std::uint32_t v) {
        next[prev[v]] = next[v];
        prev[next[v]] = prev[v];
        prev[v] = v;
        next[v] = v;
        --count;
    };

    while (count > 3) {
        bool progressed = false;
        for (std::uint32_t v = 0; v < n; ++v) {
            if (prev[v] == v || next[v] == v) {
                continue; // removed
            }
            const std::uint32_t p = prev[v];
            const std::uint32_t nx = next[v];
            const int s = orient_sign(ring[p], ring[v], ring[nx]);
            if (s < 0) {
                continue; // reflex
            }
            if (s == 0) {
                remove(v); // collinear: drop without emitting a triangle
                progressed = true;
                break;
            }
            bool blocked = false;
            for (std::uint32_t u = 0; u < n && !blocked; ++u) {
                if (u == p || u == v || u == nx || prev[u] == u || next[u] == u) {
                    continue;
                }
                if (strictly_inside_triangle(ring[p], ring[v], ring[nx], ring[u])) {
                    blocked = true;
                    break;
                }
                // A vertex exactly on the closing edge also invalidates the
                // cut: the L-shape fixture (0,0)-(2u,0)-(2u,u)-(u,u)-(u,2u)-
                // (0,2u) fails exactly here — its reflex corner (u,u) sits on
                // the closing diagonal of the corner ear. Coincident pinch
                // duplicates from hole bridging equal a corner, never a point
                // strictly between, so they stay allowed.
                if (orient_sign(ring[p], ring[nx], ring[u]) == 0) {
                    const Pt& a = ring[p];
                    const Pt& b = ring[nx];
                    const Pt& q = ring[u];
                    const bool between = (q.x > std::min(a.x, b.x) && q.x < std::max(a.x, b.x)) ||
                                         (q.y > std::min(a.y, b.y) && q.y < std::max(a.y, b.y));
                    if (between) {
                        blocked = true;
                    }
                }
            }
            if (blocked) {
                continue;
            }
            triangles.push_back(p);
            triangles.push_back(v);
            triangles.push_back(nx);
            remove(v);
            progressed = true;
            break;
        }
        if (!progressed) {
            return Result<std::vector<std::uint32_t>>::err(
                sector_error(source, sector, "triangulation failed: no valid ear remains"));
        }
    }

    // Final triangle of the live list (exactly three vertices remain).
    std::size_t a = 0;
    while (a < n && (prev[a] == a || next[a] == a)) {
        ++a;
    }
    const auto av = static_cast<std::uint32_t>(a);
    const std::uint32_t b = next[av];
    const std::uint32_t c = next[b];
    const int s = orient_sign(ring[av], ring[b], ring[c]);
    if (s > 0) {
        triangles.push_back(av);
        triangles.push_back(b);
        triangles.push_back(c);
    } else if (s < 0) {
        return Result<std::vector<std::uint32_t>>::err(
            sector_error(source, sector, "triangulation failed: residual winding flipped"));
    }
    return Result<std::vector<std::uint32_t>>::ok(std::move(triangles));
}

struct Triangulation {
    std::vector<Pt> points; // merged ring, CCW (holes pinched in)
    std::vector<std::uint32_t> triangles;
};

Result<Triangulation> triangulate_sector(const mapv7::MapData& map, std::size_t s,
                                         const SectorLoops& loops) {
    const auto sector_index = static_cast<std::int64_t>(s);
    const WallLoop& outer = loops.loops[loops.outer];

    std::vector<Pt> merged;
    merged.reserve(outer.walls.size());
    for (const std::size_t wall : outer.walls) {
        merged.push_back(Pt{map.walls[wall].x, map.walls[wall].y});
    }
    if (!outer.ccw) {
        std::reverse(merged.begin(), merged.end()); // normalize outer CCW
    }
    const std::vector<Pt> outer_ring = merged; // for hole containment tests
    S128 expected = shoelace2(merged);
    if (expected.sign() <= 0) {
        return Result<Triangulation>::err(sector_error(
            map.source, sector_index, "triangulation failed: degenerate outer loop (zero area)"));
    }

    // Normalize every hole to CW first: bridging needs the full picture so a
    // bridge can be tested against holes not yet spliced.
    std::vector<std::vector<Pt>> holes;
    for (std::size_t li = 0; li < loops.loops.size(); ++li) {
        if (li == loops.outer) {
            continue;
        }
        const WallLoop& stored = loops.loops[li];
        std::vector<Pt> hole;
        hole.reserve(stored.walls.size());
        for (const std::size_t wall : stored.walls) {
            hole.push_back(Pt{map.walls[wall].x, map.walls[wall].y});
        }
        if (stored.ccw) {
            std::reverse(hole.begin(), hole.end()); // normalize holes CW
        }
        for (const Pt& p : hole) {
            if (classify_point(p, outer_ring) != PipClass::Inside) {
                return Result<Triangulation>::err(
                    sector_error(map.source, sector_index,
                                 "triangulation failed: hole loop vertex not strictly inside the "
                                 "outer loop (touching or outside boundaries are unsupported)"));
            }
        }
        holes.push_back(std::move(hole));
    }

    std::vector<std::uint8_t> is_pinch(merged.size(), 0);
    for (std::size_t h = 0; h < holes.size(); ++h) {
        std::vector<std::vector<Pt>> later(holes.begin() + static_cast<std::ptrdiff_t>(h) + 1,
                                           holes.end());
        auto bridged = bridge_hole(map.source, sector_index, merged, is_pinch, holes[h], later);
        if (!bridged.is_ok()) {
            return Result<Triangulation>::err(bridged.error());
        }
        expected.add(shoelace2(holes[h])); // CW: negative signed area
    }

    // Exact invariant: the spliced ring encloses outer minus holes.
    const S128 merged_area = shoelace2(merged);
    if (!signed_equal(merged_area, expected)) {
        return Result<Triangulation>::err(sector_error(map.source, sector_index,
                                                       "triangulation failed: bridge construction "
                                                       "rejected by the exact area invariant"));
    }

    auto clipped = ear_clip(map.source, sector_index, merged);
    if (!clipped.is_ok()) {
        return Result<Triangulation>::err(clipped.error());
    }

    // Exact invariant: the triangles tile the spliced ring.
    S128 triangle_sum;
    for (std::size_t t = 0; t + 2 < clipped.value().size(); t += 3) {
        triangle_sum.add(triangle_area2(merged[clipped.value()[t]], merged[clipped.value()[t + 1]],
                                        merged[clipped.value()[t + 2]]));
    }
    if (!signed_equal(triangle_sum, merged_area)) {
        return Result<Triangulation>::err(
            sector_error(map.source, sector_index,
                         "triangulation failed: triangles rejected by the exact area invariant"));
    }

    Triangulation out;
    out.points = std::move(merged);
    out.triangles = clipped.take();
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

        StructuralSurface floor;
        floor.kind = SurfaceKind::Floor;
        floor.sector = static_cast<std::int16_t>(s);
        floor.wall = -1;
        floor.picnum = sector.floorpicnum;
        floor.vertices.reserve(tri.value().points.size());
        for (const Pt& p : tri.value().points) {
            floor.vertices.push_back(to_render_space(static_cast<std::int32_t>(p.x),
                                                     static_cast<std::int32_t>(p.y), sector.floorz,
                                                     options));
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
