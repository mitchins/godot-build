#include "fauxbuild/prepared.hpp"

#include <algorithm>
#include <cmath>

namespace fauxbuild {

namespace {

// --- the one UV authority ---------------------------------------------------
// Every provisional convention is applied here and nowhere else. A consumer
// that recomputes a UV is a defect, and check_layering pins that.

struct TexelScale {
    double units_per_tile_u = 1.0;
    double units_per_tile_v = 1.0;
};

// Repeat is a pixel-size control (PROVENANCE row 9): a larger repeat means
// larger pixels, so one texel spans MORE world units. The reference repeat is
// only the point the provisional constants are stated at -- it is NOT a claim
// about the default, which is not established.
double repeat_factor(std::uint8_t repeat, double reference) {
    if (repeat == 0 || reference <= 0.0) {
        return 1.0;
    }
    return static_cast<double>(repeat) / reference;
}

TexelScale wall_scale(const UvConventions& c, const SurfaceAppearance& appearance,
                      std::int16_t tile_w, std::int16_t tile_h) {
    TexelScale s;
    const double w = tile_w > 0 ? static_cast<double>(tile_w) : 1.0;
    const double h = tile_h > 0 ? static_cast<double>(tile_h) : 1.0;
    s.units_per_tile_u =
        c.wall_units_per_texel_u * repeat_factor(appearance.xrepeat, c.reference_repeat) * w;
    s.units_per_tile_v =
        c.wall_z_per_texel_v * repeat_factor(appearance.yrepeat, c.reference_repeat) * h;
    return s;
}

TexelScale floor_scale(const UvConventions& c, std::int16_t tile_w, std::int16_t tile_h) {
    TexelScale s;
    const double w = tile_w > 0 ? static_cast<double>(tile_w) : 1.0;
    const double h = tile_h > 0 ? static_cast<double>(tile_h) : 1.0;
    s.units_per_tile_u = c.floor_units_per_texel * w;
    s.units_per_tile_v = c.floor_units_per_texel * h;
    return s;
}

// Render space back to Build coordinates. to_render_space is
// (x*s, -z*s/16, y*s), so this is its inverse and nothing else.
struct BuildPoint {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

BuildPoint to_build(const StructuralVertex& v, double scale) {
    BuildPoint p;
    p.x = v.x / scale;
    p.y = v.z / scale;
    p.z = -v.y / (scale / kBuildVerticalUnitsPerHorizontal);
    return p;
}

PreparedUV make_uv(double u, double v) {
    PreparedUV uv;
    uv.u = static_cast<float>(u);
    uv.v = static_cast<float>(v);
    return uv;
}

// Authored placement (M6.2B1). Panning bytes are texel offsets within the
// selected tile -> tile-local phase; the SIGN is the one global provisional
// choice (UvConventions::panning_adds_phase). Flips are mirrors: they negate
// the position-derived coordinate only, so a pan applied after a flip is
// never erased or doubled.
struct PanPhase {
    double u = 0.0;
    double v = 0.0;
};

PanPhase pan_phase(const SurfaceAppearance& appearance, std::int16_t tile_w, std::int16_t tile_h,
                   const UvConventions& c) {
    const double w = tile_w > 0 ? static_cast<double>(tile_w) : 1.0;
    const double h = tile_h > 0 ? static_cast<double>(tile_h) : 1.0;
    const double sign = c.panning_adds_phase ? 1.0 : -1.0;
    PanPhase pan;
    pan.u = sign * (static_cast<double>(appearance.xpanning) / w);
    pan.v = sign * (static_cast<double>(appearance.ypanning) / h);
    return pan;
}

// The sector's authored texture frame for relative alignment (bit 0x0040).
// Returns false when the frame is absent or degenerate; the caller then
// stays on world axes (build emitted a `relative_alignment_no_frame` note).
// The frame is CONSUMED from StructuralWorld::sector_frames — never
// reconstructed from emitted surfaces; ci/check_layering.py pins that.
bool relative_frame(const StructuralSectorFrame& frame, const UvConventions& c, double& ux,
                    double& uy, double& vx, double& vy, double& ox, double& oy) {
    if (frame.first_wall < 0 || (frame.ax == frame.bx && frame.ay == frame.by)) {
        return false;
    }
    const double dx = static_cast<double>(frame.bx) - static_cast<double>(frame.ax);
    const double dy = static_cast<double>(frame.by) - static_cast<double>(frame.ay);
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0) {
        return false;
    }
    ux = dx / len;
    uy = dy / len;
    if (!c.floor_relative_u_follows_first_wall) {
        ux = -ux;
        uy = -uy;
    }
    // Left perpendicular of the directed first wall.
    vx = -uy;
    vy = ux;
    if (!c.floor_relative_v_is_left_perp) {
        vx = -vx;
        vy = -vy;
    }
    ox = static_cast<double>(frame.ax);
    oy = static_cast<double>(frame.ay);
    return true;
}

void compute_uvs(const StructuralSurface& surface, const StructuralSectorFrame& frame,
                 const UvConventions& c, std::int16_t tile_w, std::int16_t tile_h,
                 std::vector<PreparedUV>& out) {
    out.clear();
    out.reserve(surface.vertices.size());

    const std::int16_t raw = surface.appearance.raw_stat;
    const PanPhase pan = pan_phase(surface.appearance, tile_w, tile_h, c);

    const bool is_flat = surface.kind == SurfaceKind::Floor || surface.kind == SurfaceKind::Ceiling;
    if (is_flat) {
        // PROVISIONAL: floor/ceiling textures are anchored to ABSOLUTE world
        // coordinates (PROVENANCE row 16, established), so U/V come straight
        // from Build X/Y with no sector-relative origin — UNLESS the authored
        // relative-alignment bit selects the sector's first-wall frame.
        const TexelScale s = floor_scale(c, tile_w, tile_h);
        double ux = 0.0, uy = 0.0, vx = 0.0, vy = 0.0, ox = 0.0, oy = 0.0;
        const bool have_relative = (raw & mapv7::kStatPlaneRelative) != 0 &&
                                   relative_frame(frame, c, ux, uy, vx, vy, ox, oy);
        for (const auto& vertex : surface.vertices) {
            const BuildPoint p = to_build(vertex, c.render_scale);
            double a = 0.0;
            double b = 0.0;
            if (have_relative) {
                a = (p.x - ox) * ux + (p.y - oy) * uy;
                b = (p.x - ox) * vx + (p.y - oy) * vy;
            } else {
                a = c.floor_u_is_world_x ? p.x : p.y;
                b = c.floor_u_is_world_x ? p.y : p.x;
            }
            // Swap-XY exchanges the base axes (documented bit meaning).
            if ((raw & mapv7::kStatPlaneSwapXY) != 0) {
                double t = a;
                a = b;
                b = t;
            }
            double u = a / s.units_per_tile_u;
            double v = b / s.units_per_tile_v;
            if ((raw & mapv7::kStatPlaneFlipX) != 0) {
                u = -u;
            }
            if ((raw & mapv7::kStatPlaneFlipY) != 0) {
                v = -v;
            }
            if (surface.appearance.xpanning != 0) {
                u += pan.u;
            }
            if (surface.appearance.ypanning != 0) {
                v += pan.v;
            }
            out.push_back(make_uv(u, v));
        }
        return;
    }

    // Wall span. U runs along the span's own horizontal direction from its
    // first vertex; V runs down Build Z from the span's anchor edge — the
    // TOP edge by default, the BOTTOM edge with the documented bottom-align
    // bit ("align picture on bottom", PROVENANCE row 9). The texel scale is
    // the same either way; only the anchor moves.
    const TexelScale s = wall_scale(c, surface.appearance, tile_w, tile_h);
    if (surface.vertices.empty()) {
        return;
    }
    const BuildPoint origin = to_build(surface.vertices[0], c.render_scale);
    double dir_x = 0.0;
    double dir_y = 0.0;
    for (const auto& vertex : surface.vertices) {
        const BuildPoint p = to_build(vertex, c.render_scale);
        const double dx = p.x - origin.x;
        const double dy = p.y - origin.y;
        if (dx != 0.0 || dy != 0.0) {
            dir_x = dx;
            dir_y = dy;
            break;
        }
    }
    const double length = std::sqrt(dir_x * dir_x + dir_y * dir_y);
    if (length > 0.0) {
        dir_x /= length;
        dir_y /= length;
    }
    if (!c.wall_u_follows_wall_direction) {
        dir_x = -dir_x;
        dir_y = -dir_y;
    }

    // Anchor V at the span's topmost point (smallest Build Z; Build Z grows
    // downward) or, bottom-aligned, at its lowest point. PROVISIONAL: which
    // end anchors is missing fact 6; on sloped spans the anchor is the
    // span's own extreme vertex, the generic vertical-coordinate model.
    double anchor_z = to_build(surface.vertices[0], c.render_scale).z;
    const bool bottom_aligned = (raw & mapv7::kWallCstatBottomAligned) != 0;
    for (const auto& vertex : surface.vertices) {
        const double z = to_build(vertex, c.render_scale).z;
        anchor_z = bottom_aligned ? std::max(anchor_z, z) : std::min(anchor_z, z);
    }

    for (const auto& vertex : surface.vertices) {
        const BuildPoint p = to_build(vertex, c.render_scale);
        const double along = (p.x - origin.x) * dir_x + (p.y - origin.y) * dir_y;
        const double down = c.wall_v_increases_with_build_z ? (p.z - anchor_z) : (anchor_z - p.z);
        double u = along / s.units_per_tile_u;
        double v = down / s.units_per_tile_v;
        if ((raw & mapv7::kWallCstatFlipX) != 0) {
            u = -u;
        }
        if ((raw & mapv7::kWallCstatFlipY) != 0) {
            v = -v;
        }
        if (surface.appearance.xpanning != 0) {
            u += pan.u;
        }
        if (surface.appearance.ypanning != 0) {
            v += pan.v;
        }
        out.push_back(make_uv(u, v));
    }
}

} // namespace

Result<PreparedWorld> prepare_world(const StructuralWorld& world, const IndexedAtlas& atlas,
                                    const PaletteData& palette, const UvConventions& conventions) {
    PreparedWorld prepared;
    prepared.surfaces.reserve(world.surfaces.size());

    // The per-sector tables are part of the D0020 seam CONTRACT, not an
    // internal invariant: `world` is caller-provided, so an incoherent table
    // is external input and must produce a structured error, never an
    // FB_CHECK and never a silent read past the end. Both tables share one
    // index domain (a surface's `sector`), so the domain is validated once,
    // up front, before any surface is prepared -- a partially built
    // PreparedWorld from garbage frames is worse than no PreparedWorld.
    const std::size_t sector_domain = world.sector_appearance.size();
    if (world.sector_frames.size() != sector_domain) {
        return Result<PreparedWorld>::err(
            {"prepared", 0, "world", ErrorCode::InvalidTopology,
             "sector_frames has " + std::to_string(world.sector_frames.size()) +
                 " entries but the sector index domain is " + std::to_string(sector_domain) +
                 " (sector_appearance); both tables must cover every source sector"});
    }
    for (std::size_t i = 0; i < world.surfaces.size(); ++i) {
        const std::int16_t sector = world.surfaces[i].sector;
        if (sector < 0 || static_cast<std::size_t>(sector) >= sector_domain) {
            return Result<PreparedWorld>::err({"prepared", static_cast<std::uint64_t>(i), "surface",
                                               ErrorCode::InvalidRange,
                                               "surface sector " + std::to_string(sector) +
                                                   " is outside the sector index domain [0, " +
                                                   std::to_string(sector_domain) + ")"});
        }
    }

    for (std::size_t i = 0; i < world.surfaces.size(); ++i) {
        const StructuralSurface& surface = world.surfaces[i];
        const auto picnum = static_cast<std::uint32_t>(surface.appearance.picnum);
        if (surface.appearance.picnum < 0 || picnum >= atlas.tiles.size()) {
            return Result<PreparedWorld>::err(
                {"prepared", static_cast<std::uint64_t>(i), "surface", ErrorCode::InvalidName,
                 "picnum " + std::to_string(surface.appearance.picnum) +
                     " is outside the atlas tile namespace"});
        }
        const AtlasTileEntry& tile = atlas.tiles[picnum];
        if (!tile.populated || tile.page < 0 || tile.width <= 0 || tile.height <= 0) {
            return Result<PreparedWorld>::err(
                {"prepared", static_cast<std::uint64_t>(i), "surface", ErrorCode::Unsupported,
                 "picnum " + std::to_string(surface.appearance.picnum) +
                     " has no populated atlas tile"});
        }

        PreparedSurface out;
        out.kind = surface.kind;
        out.sector = surface.sector;
        out.wall = surface.wall;
        out.appearance = surface.appearance;
        out.page = tile.page;
        out.picnum = static_cast<std::int32_t>(picnum);
        // Geometry passes through verbatim: preparation adds UVs, nothing else.
        out.vertices = surface.vertices;
        out.indices = surface.indices;

        const double pw = atlas.page_width > 0 ? static_cast<double>(atlas.page_width) : 1.0;
        const double ph = atlas.page_height > 0 ? static_cast<double>(atlas.page_height) : 1.0;
        out.rect_x = static_cast<float>(static_cast<double>(tile.x) / pw);
        out.rect_y = static_cast<float>(static_cast<double>(tile.y) / ph);
        out.rect_w = static_cast<float>(static_cast<double>(tile.width) / pw);
        out.rect_h = static_cast<float>(static_cast<double>(tile.height) / ph);

        compute_uvs(surface, world.sector_frames[static_cast<std::size_t>(surface.sector)],
                    conventions, tile.width, tile.height, out.uvs);
        prepared.surfaces.push_back(std::move(out));
    }

    prepared.atlas_pixels = atlas.pixels;
    prepared.page_width = atlas.page_width;
    prepared.page_height = atlas.page_height;
    prepared.page_count = atlas.page_count;

    // Base palette only for this slice: 6-bit stored values expanded to 8-bit.
    prepared.palette_rgb.reserve(kPaletteBytes);
    for (std::size_t i = 0; i < kPaletteBytes; ++i) {
        const auto six = static_cast<std::uint32_t>(palette.rgb[i]);
        prepared.palette_rgb.push_back(static_cast<std::uint8_t>((six * 255u) / 63u));
    }
    return Result<PreparedWorld>::ok(std::move(prepared));
}

} // namespace fauxbuild
