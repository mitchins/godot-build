#include "fauxbuild/prepared.hpp"

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

PreparedUV make_uv(double u, double v) {
    PreparedUV uv;
    uv.u = static_cast<float>(u);
    uv.v = static_cast<float>(v);
    return uv;
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

void compute_uvs(const StructuralSurface& surface, const UvConventions& c, std::int16_t tile_w,
                 std::int16_t tile_h, std::vector<PreparedUV>& out) {
    out.clear();
    out.reserve(surface.vertices.size());

    const bool is_flat = surface.kind == SurfaceKind::Floor || surface.kind == SurfaceKind::Ceiling;
    if (is_flat) {
        // PROVISIONAL: floor/ceiling textures are anchored to ABSOLUTE world
        // coordinates (PROVENANCE row 16, established), so U/V come straight
        // from Build X/Y with no sector-relative origin.
        const TexelScale s = floor_scale(c, tile_w, tile_h);
        for (const auto& vertex : surface.vertices) {
            const BuildPoint p = to_build(vertex, c.render_scale);
            const double a = c.floor_u_is_world_x ? p.x : p.y;
            const double b = c.floor_u_is_world_x ? p.y : p.x;
            out.push_back(make_uv(a / s.units_per_tile_u, b / s.units_per_tile_v));
        }
        return;
    }

    // Wall span. U runs along the span's own horizontal direction from its
    // first vertex; V runs down Build Z from the span's top.
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
    // downward). PROVISIONAL: which end anchors is missing fact 6.
    double top_z = to_build(surface.vertices[0], c.render_scale).z;
    for (const auto& vertex : surface.vertices) {
        top_z = std::min(top_z, to_build(vertex, c.render_scale).z);
    }

    for (const auto& vertex : surface.vertices) {
        const BuildPoint p = to_build(vertex, c.render_scale);
        const double along = (p.x - origin.x) * dir_x + (p.y - origin.y) * dir_y;
        const double down = c.wall_v_increases_with_build_z ? (p.z - top_z) : (top_z - p.z);
        out.push_back(make_uv(along / s.units_per_tile_u, down / s.units_per_tile_v));
    }
}

} // namespace

Result<PreparedWorld> prepare_world(const StructuralWorld& world, const IndexedAtlas& atlas,
                                    const PaletteData& palette, const UvConventions& conventions) {
    PreparedWorld prepared;
    prepared.surfaces.reserve(world.surfaces.size());

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

        compute_uvs(surface, conventions, tile.width, tile.height, out.uvs);
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
