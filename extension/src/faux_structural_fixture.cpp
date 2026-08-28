#include "fauxbuild_godot/faux_structural_fixture.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fauxbuild/art.hpp"
#include "fauxbuild/asset_set.hpp"
#include "fauxbuild/atlas.hpp"
#include "fauxbuild/grp_synth.hpp"
#include "fauxbuild/map_io.hpp"
#include "fauxbuild/map_synth.hpp"
#include "fauxbuild/palette.hpp"
#include "fauxbuild/prepared.hpp"
#include "fauxbuild/tile_build.hpp"
#include "fauxbuild/vfs.hpp"
#include "fauxbuild_godot/fauxbuild_view.hpp"

namespace fauxbuild_godot {

void FauxStructuralFixture::_bind_methods() {
    using godot::ClassDB;
    ClassDB::bind_method(godot::D_METHOD("get_fixture_names"),
                         &FauxStructuralFixture::get_fixture_names);
    ClassDB::bind_method(godot::D_METHOD("present", "name", "view"),
                         &FauxStructuralFixture::present);
    ClassDB::bind_method(godot::D_METHOD("write_fixture_map", "name", "directory"),
                         &FauxStructuralFixture::write_fixture_map);
    ClassDB::bind_method(godot::D_METHOD("write_placement_map", "directory"),
                         &FauxStructuralFixture::write_placement_map);
    ClassDB::bind_method(godot::D_METHOD("write_masked_map", "directory"),
                         &FauxStructuralFixture::write_masked_map);
    ClassDB::bind_method(godot::D_METHOD("write_fixture_grp", "entries", "directory", "file_name"),
                         &FauxStructuralFixture::write_fixture_grp);
    ClassDB::bind_method(godot::D_METHOD("write_fixture_assets", "directory"),
                         &FauxStructuralFixture::write_fixture_assets);
    ClassDB::bind_method(godot::D_METHOD("prepare_from_dir", "dir_path", "map_name"),
                         &FauxStructuralFixture::prepare_from_dir);
    ClassDB::bind_method(godot::D_METHOD("prepared_vertices"),
                         &FauxStructuralFixture::prepared_vertices);
    ClassDB::bind_method(godot::D_METHOD("prepared_uvs"), &FauxStructuralFixture::prepared_uvs);
    ClassDB::bind_method(godot::D_METHOD("present_prepared_with_page", "view", "page"),
                         &FauxStructuralFixture::present_prepared_with_page);
    ClassDB::bind_method(godot::D_METHOD("get_last_error"), &FauxStructuralFixture::get_last_error);
    ClassDB::bind_method(godot::D_METHOD("expected_surface_count", "kind"),
                         &FauxStructuralFixture::expected_surface_count);
    ClassDB::bind_method(godot::D_METHOD("expected_vertices", "kind"),
                         &FauxStructuralFixture::expected_vertices);
    ClassDB::bind_method(godot::D_METHOD("expected_indices", "kind"),
                         &FauxStructuralFixture::expected_indices);
    ClassDB::bind_method(godot::D_METHOD("get_note_count"), &FauxStructuralFixture::get_note_count);
    ClassDB::bind_method(godot::D_METHOD("get_diagnostic_count"),
                         &FauxStructuralFixture::get_diagnostic_count);
}

godot::PackedStringArray FauxStructuralFixture::get_fixture_names() const {
    godot::PackedStringArray names;
    for (const auto& name : fauxbuild::synth::map_fixture_names()) {
        names.push_back(name.c_str());
    }
    return names;
}

godot::String FauxStructuralFixture::get_last_error() const {
    return last_error_;
}

bool FauxStructuralFixture::present(const godot::String& name, FauxBuildView* view) {
    last_error_ = "";
    if (view == nullptr) {
        last_error_ = "no FauxBuildView given";
        return false;
    }

    auto map = fauxbuild::synth::map_fixture(name.utf8().get_data());
    if (!map.is_ok()) {
        last_error_ = "fixture '" + name + "': " + map.error().to_string().c_str();
        return false;
    }

    auto world = fauxbuild::build_structural_world(map.value());
    if (!world.is_ok()) {
        last_error_ =
            "structural derivation of '" + name + "': " + world.error().to_string().c_str();
        return false;
    }

    if (!view->present_world(world.value())) {
        last_error_ = "FauxBuildView rejected the world (see errors above)";
        return false;
    }

    world_ = std::make_shared<const fauxbuild::StructuralWorld>(std::move(world.value()));
    return true;
}

namespace {

// GDScript callers pass 0..5; anything else is a caller bug and fails
// cleanly (empty result) rather than throwing across the binding boundary.
bool kind_from_int(std::int32_t kind, fauxbuild::SurfaceKind* out) {
    switch (kind) {
    case 0:
        *out = fauxbuild::SurfaceKind::Floor;
        return true;
    case 1:
        *out = fauxbuild::SurfaceKind::Ceiling;
        return true;
    case 2:
        *out = fauxbuild::SurfaceKind::SolidWall;
        return true;
    case 3:
        *out = fauxbuild::SurfaceKind::PortalUpper;
        return true;
    case 4:
        *out = fauxbuild::SurfaceKind::PortalLower;
        return true;
    case 5:
        *out = fauxbuild::SurfaceKind::PortalMasked;
        return true;
    default:
        return false;
    }
}

} // namespace

godot::String FauxStructuralFixture::write_fixture_map(const godot::String& name,
                                                       const godot::String& directory) {
    last_error_ = "";
    const std::string fixture = name.utf8().get_data();
    const std::string dir = directory.utf8().get_data();
    if (fixture.empty() || dir.empty()) {
        last_error_ = "write_fixture_map needs a fixture name and an output directory";
        return "";
    }

    auto bytes = fauxbuild::synth::serialize_map_fixture(fixture);
    if (!bytes.is_ok()) {
        last_error_ = "fixture '" + name + "': " + bytes.error().to_string().c_str();
        return "";
    }

    // The on-disk filename is the normalized VFS key, so the production
    // route can look the file up by the returned name through a plain
    // directory mount.
    std::string upper;
    upper.reserve(fixture.size());
    for (const char c : fixture) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    const std::string vfs_name = upper + ".MAP";
    const std::filesystem::path out = std::filesystem::path(dir) / vfs_name;

    std::ofstream stream(out, std::ios::binary | std::ios::trunc);
    if (!stream) {
        last_error_ = "cannot open '" + godot::String(out.string().c_str()) + "' for writing";
        return "";
    }
    stream.write(reinterpret_cast<const char*>(bytes.value().data()),
                 static_cast<std::streamsize>(bytes.value().size()));
    stream.close();
    if (!stream) {
        last_error_ = "failed writing '" + godot::String(out.string().c_str()) + "'";
        return "";
    }
    return vfs_name.c_str();
}

godot::String FauxStructuralFixture::write_placement_map(const godot::String& directory) {
    // M6.2B1: the same two_sector_portal geometry with AUTHORED placement
    // fields set, so the textured boundary gate exercises panning, flips,
    // swap-XY, relative alignment and wall bottom alignment THROUGH the
    // consumer boundary — verbatim passthrough of placement-carrying UVs,
    // not only of default-flag UVs. All values are original synthetic
    // content; picnums address the gate tileset (0 and 1).
    last_error_ = "";
    const std::string dir = directory.utf8().get_data();
    if (dir.empty()) {
        last_error_ = "write_placement_map needs an output directory";
        return "";
    }
    auto map = fauxbuild::synth::map_fixture("two_sector_portal");
    if (!map.is_ok()) {
        last_error_ = godot::String("fixture: ") + map.error().to_string().c_str();
        return "";
    }
    auto& data = map.value();
    // Sector 0 floor: relative alignment + both pans + x-flip.
    data.sectors[0].floorpicnum = 0;
    data.sectors[0].floorstat |=
        fauxbuild::mapv7::kStatPlaneRelative | fauxbuild::mapv7::kStatPlaneFlipX;
    data.sectors[0].floorxpanning = 24;
    data.sectors[0].floorypanning = 8;
    // Sector 0 ceiling: y-flip + swap-XY.
    data.sectors[0].ceilingpicnum = 1;
    data.sectors[0].ceilingstat |=
        fauxbuild::mapv7::kStatPlaneFlipY | fauxbuild::mapv7::kStatPlaneSwapXY;
    // Sector 1 floor: plain world-anchored with a pan (control population).
    data.sectors[1].floorpicnum = 1;
    data.sectors[1].floorxpanning = 16;
    // Sector 1 ceiling: relative alignment without other controls.
    data.sectors[1].ceilingpicnum = 0;
    data.sectors[1].ceilingstat |= fauxbuild::mapv7::kStatPlaneRelative;
    // Walls: bottom alignment, flips and panning across the population;
    // picnums alternate so grouping also sees both tiles.
    for (std::size_t i = 0; i < data.walls.size(); ++i) {
        auto& wall = data.walls[i];
        wall.picnum = static_cast<std::int16_t>(i % 2);
        wall.xrepeat = 16;
        wall.yrepeat = 16;
        switch (i % 4) {
        case 0:
            wall.cstat |= fauxbuild::mapv7::kWallCstatBottomAligned;
            break;
        case 1:
            wall.cstat |= fauxbuild::mapv7::kWallCstatFlipX;
            wall.xpanning = 32;
            break;
        case 2:
            wall.cstat |= fauxbuild::mapv7::kWallCstatFlipY;
            wall.ypanning = 48;
            break;
        default:
            wall.xpanning = 16;
            wall.ypanning = 16;
            break;
        }
    }
    auto bytes = fauxbuild::write_map(data);
    if (!bytes.is_ok()) {
        last_error_ = godot::String("serialize: ") + bytes.error().to_string().c_str();
        return "";
    }
    const std::string vfs_name = "PLACEMENT.MAP";
    const std::filesystem::path out = std::filesystem::path(dir) / vfs_name;
    std::ofstream stream(out, std::ios::binary | std::ios::trunc);
    if (!stream) {
        last_error_ = "cannot open '" + godot::String(out.string().c_str()) + "' for writing";
        return "";
    }
    stream.write(reinterpret_cast<const char*>(bytes.value().data()),
                 static_cast<std::streamsize>(bytes.value().size()));
    stream.close();
    if (!stream) {
        last_error_ = "failed writing '" + godot::String(out.string().c_str()) + "'";
        return "";
    }
    return vfs_name.c_str();
}

godot::String FauxStructuralFixture::write_masked_map(const godot::String& directory) {
    // M6.2C1: the masked-layer boundary gate content. The portal_heights
    // window geometry with authored masked walls: the portal walls carry the
    // masked bit with overpicnum 0 (TILE 0 — the zero case, gate_a) on one
    // side and overpicnum 1 (gate_b) on the other; a solid wall carries the
    // masked bit with a nonzero overpicnum (no opening exists, no layer may
    // be manufactured); and a plain solid wall carries a nonzero overpicnum
    // WITHOUT the bit (the converse: preserved, consumed by nothing). All
    // original synthetic content; picnums address the gate tileset.
    last_error_ = "";
    const std::string dir = directory.utf8().get_data();
    if (dir.empty()) {
        last_error_ = "write_masked_map needs an output directory";
        return "";
    }
    auto map = fauxbuild::synth::map_fixture("portal_heights");
    if (!map.is_ok()) {
        last_error_ = godot::String("fixture: ") + map.error().to_string().c_str();
        return "";
    }
    auto& data = map.value();
    // Masked portal: both sides of the shared edge, distinct overlay tiles.
    data.walls[1].cstat |= fauxbuild::mapv7::kWallCstatMasked;
    data.walls[1].overpicnum = 0; // tile 0, deliberately — no zero sentinel
    data.walls[7].cstat |= fauxbuild::mapv7::kWallCstatMasked;
    data.walls[7].overpicnum = 1;
    // Solid wall with the masked bit: reported, no layer manufactured.
    data.walls[0].cstat |= fauxbuild::mapv7::kWallCstatMasked;
    data.walls[0].overpicnum = 1;
    // Solid wall with a nonzero overpicnum and NO masked bit: the converse.
    data.walls[2].overpicnum = 1;
    for (auto& wall : data.walls) {
        wall.xrepeat = 16;
        wall.yrepeat = 16;
    }
    data.sectors[0].floorpicnum = 0;
    data.sectors[0].ceilingpicnum = 1;
    data.sectors[1].floorpicnum = 1;
    data.sectors[1].ceilingpicnum = 0;
    auto bytes = fauxbuild::write_map(data);
    if (!bytes.is_ok()) {
        last_error_ = godot::String("serialize: ") + bytes.error().to_string().c_str();
        return "";
    }
    const std::string vfs_name = "MASKED.MAP";
    const std::filesystem::path out = std::filesystem::path(dir) / vfs_name;
    std::ofstream stream(out, std::ios::binary | std::ios::trunc);
    if (!stream) {
        last_error_ = "cannot open '" + godot::String(out.string().c_str()) + "' for writing";
        return "";
    }
    stream.write(reinterpret_cast<const char*>(bytes.value().data()),
                 static_cast<std::streamsize>(bytes.value().size()));
    stream.close();
    if (!stream) {
        last_error_ = "failed writing '" + godot::String(out.string().c_str()) + "'";
        return "";
    }
    return vfs_name.c_str();
}

godot::String FauxStructuralFixture::write_fixture_grp(const godot::Array& entries,
                                                       const godot::String& directory,
                                                       const godot::String& file_name) {
    last_error_ = "";
    if (entries.is_empty() || directory.is_empty() || file_name.is_empty()) {
        last_error_ = "write_fixture_grp needs entries, an output directory and a file name";
        return "";
    }

    std::vector<fauxbuild::synth::GrpFileSpec> files;
    files.reserve(static_cast<std::size_t>(entries.size()));
    for (int i = 0; i < entries.size(); ++i) {
        const godot::Dictionary entry = entries[i];
        const godot::String fixture = entry.get("fixture", "");
        const godot::String vfs_name = entry.get("name", "");
        const bool corrupt = entry.get("corrupt", false);
        if (fixture.is_empty() || vfs_name.is_empty()) {
            last_error_ = "write_fixture_grp entry needs 'fixture' and 'name'";
            return "";
        }

        auto bytes = fauxbuild::synth::serialize_map_fixture(fixture.utf8().get_data());
        if (!bytes.is_ok()) {
            last_error_ = "fixture '" + fixture + "': " + bytes.error().to_string().c_str();
            return "";
        }
        auto payload = std::move(bytes.value());
        if (corrupt) {
            // Damage the version field the MAP reader checks first, so the
            // archive stays well-formed and only its payload is bad: the
            // failure must come from parsing mounted bytes, not from the
            // GRP container.
            if (payload.empty()) {
                last_error_ = "fixture '" + fixture + "' serialized to nothing";
                return "";
            }
            payload[0] = 99;
        }
        files.push_back({std::string(vfs_name.utf8().get_data()), std::move(payload)});
    }

    const auto image = fauxbuild::synth::build_grp(files);
    const std::filesystem::path out =
        std::filesystem::path(directory.utf8().get_data()) / file_name.utf8().get_data();

    std::ofstream stream(out, std::ios::binary | std::ios::trunc);
    if (!stream) {
        last_error_ = "cannot open '" + godot::String(out.string().c_str()) + "' for writing";
        return "";
    }
    stream.write(reinterpret_cast<const char*>(image.data()),
                 static_cast<std::streamsize>(image.size()));
    stream.close();
    if (!stream) {
        last_error_ = "failed writing '" + godot::String(out.string().c_str()) + "'";
        return "";
    }
    return out.string().c_str();
}

std::int32_t FauxStructuralFixture::expected_surface_count(std::int32_t kind) const {
    fauxbuild::SurfaceKind surface_kind;
    if (!world_ || !kind_from_int(kind, &surface_kind)) {
        return 0;
    }
    std::int32_t count = 0;
    for (const auto& surface : world_->surfaces) {
        if (surface.kind == surface_kind) {
            ++count;
        }
    }
    return count;
}

godot::PackedVector3Array FauxStructuralFixture::expected_vertices(std::int32_t kind) const {
    godot::PackedVector3Array out;
    fauxbuild::SurfaceKind surface_kind;
    if (!world_ || !kind_from_int(kind, &surface_kind)) {
        return out;
    }
    for (const auto& surface : world_->surfaces) {
        if (surface.kind != surface_kind) {
            continue;
        }
        for (const auto& vertex : surface.vertices) {
            out.push_back(godot::Vector3(vertex.x, vertex.y, vertex.z));
        }
    }
    return out;
}

godot::PackedInt32Array FauxStructuralFixture::expected_indices(std::int32_t kind) const {
    godot::PackedInt32Array out;
    fauxbuild::SurfaceKind surface_kind;
    if (!world_ || !kind_from_int(kind, &surface_kind)) {
        return out;
    }
    std::int64_t offset = 0;
    for (const auto& surface : world_->surfaces) {
        if (surface.kind != surface_kind) {
            continue;
        }
        for (const std::uint32_t index : surface.indices) {
            out.push_back(static_cast<std::int32_t>(offset + index));
        }
        offset += static_cast<std::int64_t>(surface.vertices.size());
    }
    return out;
}

std::int32_t FauxStructuralFixture::get_note_count() const {
    return world_ ? static_cast<std::int32_t>(world_->notes.size()) : 0;
}

std::int32_t FauxStructuralFixture::get_diagnostic_count() const {
    return world_ ? static_cast<std::int32_t>(world_->diagnostics.size()) : 0;
}

namespace {

// ORIGINAL synthetic tileset for the textured boundary gate. Two tiles with
// DIFFERENT dimensions, so a tile-size dependency in the UV authority is
// exercised rather than assumed away.
constexpr const char* kGateTileset = "tileset textured_gate\n"
                                     "tile gate_a 64 64 pattern=checker a=16 b=60 square=24\n"
                                     "tile gate_b 128 64 pattern=checker a=20 b=56 square=24\n";

} // namespace

godot::String FauxStructuralFixture::write_fixture_assets(const godot::String& directory) {
    last_error_ = "";
    auto tileset = fauxbuild::parse_tileset(kGateTileset, "textured_gate");
    if (!tileset.is_ok()) {
        last_error_ = godot::String("tileset: ") + tileset.error().to_string().c_str();
        return "";
    }
    fauxbuild::TileManifest manifest;
    auto built = fauxbuild::build_art_from_tileset(tileset.value(), manifest);
    if (!built.is_ok()) {
        last_error_ = godot::String("art: ") + built.error().to_string().c_str();
        return "";
    }
    auto art_bytes = fauxbuild::write_art(built.value().art);
    if (!art_bytes.is_ok()) {
        last_error_ = godot::String("art write: ") + art_bytes.error().to_string().c_str();
        return "";
    }

    fauxbuild::PaletteData palette;
    for (std::size_t i = 0; i < fauxbuild::kPaletteBytes; ++i) {
        palette.rgb[i] = static_cast<std::uint8_t>(i % 64);
    }
    palette.num_shades = 1;
    palette.shade_tables.assign(256, 0);
    for (std::size_t i = 0; i < 256; ++i) {
        palette.shade_tables[i] = static_cast<std::uint8_t>(i);
    }
    palette.translucency.assign(65536, 0);
    auto palette_bytes = fauxbuild::write_palette_dat(palette);
    if (!palette_bytes.is_ok()) {
        last_error_ = godot::String("palette write: ") + palette_bytes.error().to_string().c_str();
        return "";
    }

    // load_asset_set requires a LOOKUP.DAT alongside the palette; the real GRP
    // has one, so the fixture must too or the gate would only ever exercise
    // the failure path.
    fauxbuild::LookupData lookup;
    auto lookup_bytes = fauxbuild::write_lookup_dat(lookup);
    if (!lookup_bytes.is_ok()) {
        last_error_ = godot::String("lookup write: ") + lookup_bytes.error().to_string().c_str();
        return "";
    }

    const std::filesystem::path dir(directory.utf8().get_data());
    for (const auto& [name, bytes] : {std::pair<const char*, const std::vector<std::uint8_t>*>{
                                          "TILES000.ART", &art_bytes.value()},
                                      std::pair<const char*, const std::vector<std::uint8_t>*>{
                                          "PALETTE.DAT", &palette_bytes.value()},
                                      std::pair<const char*, const std::vector<std::uint8_t>*>{
                                          "LOOKUP.DAT", &lookup_bytes.value()}}) {
        std::ofstream out(dir / name, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes->data()),
                  static_cast<std::streamsize>(bytes->size()));
        out.close();
        if (!out) {
            last_error_ = godot::String("cannot write ") + name;
            return "";
        }
    }
    return "TILES000.ART+PALETTE.DAT+LOOKUP.DAT";
}

bool FauxStructuralFixture::prepare_from_dir(const godot::String& dir_path,
                                             const godot::String& map_name) {
    last_error_ = "";
    prepared_vertices_.clear();
    prepared_uvs_.clear();

    auto mount = fauxbuild::DirectoryMount::create(dir_path.utf8().get_data());
    if (!mount.is_ok()) {
        last_error_ = godot::String("mount: ") + mount.error().to_string().c_str();
        return false;
    }
    fauxbuild::Vfs vfs;
    vfs.add_mount(std::move(mount.value()));
    auto file = vfs.open(map_name.utf8().get_data());
    if (!file.is_ok()) {
        last_error_ = godot::String("open: ") + file.error().to_string().c_str();
        return false;
    }
    const auto& bytes = file.value().bytes;
    auto map = fauxbuild::read_map(
        std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
        file.value().origin);
    if (!map.is_ok()) {
        last_error_ = godot::String("map: ") + map.error().to_string().c_str();
        return false;
    }
    auto world = fauxbuild::build_structural_world(map.value());
    if (!world.is_ok()) {
        last_error_ = godot::String("world: ") + world.error().to_string().c_str();
        return false;
    }
    auto assets = fauxbuild::load_asset_set(vfs);
    if (!assets.is_ok()) {
        last_error_ = godot::String("assets: ") + assets.error().to_string().c_str();
        return false;
    }
    auto atlas = fauxbuild::build_indexed_atlas(assets.value().arts, {});
    if (!atlas.is_ok()) {
        last_error_ = godot::String("atlas: ") + atlas.error().to_string().c_str();
        return false;
    }
    auto prepared = fauxbuild::prepare_world(world.value(), atlas.value(), assets.value().palette);
    if (!prepared.is_ok()) {
        last_error_ = godot::String("prepare: ") + prepared.error().to_string().c_str();
        return false;
    }
    prepared_world_ = std::make_shared<const fauxbuild::PreparedWorld>(prepared.value());
    for (const auto& surface : prepared.value().surfaces) {
        for (std::size_t i = 0; i < surface.vertices.size(); ++i) {
            const auto& v = surface.vertices[i];
            prepared_vertices_.push_back(godot::Vector3(
                static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)));
            prepared_uvs_.push_back(godot::Vector2(surface.uvs[i].u, surface.uvs[i].v));
        }
    }
    return true;
}

godot::PackedVector3Array FauxStructuralFixture::prepared_vertices() const {
    return prepared_vertices_;
}

godot::PackedVector2Array FauxStructuralFixture::prepared_uvs() const {
    return prepared_uvs_;
}

bool FauxStructuralFixture::present_prepared_with_page(FauxBuildView* view, std::int32_t page) {
    last_error_ = "";
    if (view == nullptr || prepared_world_ == nullptr) {
        last_error_ = "present_prepared_with_page needs a view and a prepared world";
        return false;
    }
    fauxbuild::PreparedWorld copy = *prepared_world_;
    for (auto& surface : copy.surfaces) {
        surface.page = page;
    }
    return view->present_prepared_world(copy);
}

} // namespace fauxbuild_godot
