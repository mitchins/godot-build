#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <memory>

#include "fauxbuild/prepared.hpp"
#include "fauxbuild/structural.hpp"

namespace fauxbuild_godot {

class FauxBuildView;

// TEST/SAMPLE bridge for the M5 slice-2 scene gates — fixture infrastructure,
// not a production world-loading API. GDScript cannot carry a native
// StructuralWorld, so this class owns the one legitimate producer path for
// synthetic scenes:
//
//     committed core fixture -> core structural derivation -> StructuralWorld
//                             -> FauxBuildView.present_world (C++ seam)
//
// It exists so the fixture scene and the human viewer can exercise the real
// production boundary. FauxBuildView itself never constructs a world; this
// class never renders one. Real-content (GRP/MAP) loading is slice 3 and
// lives elsewhere; this harness accepts fixture names only.
class FauxStructuralFixture : public godot::RefCounted {
    GDCLASS(FauxStructuralFixture, godot::RefCounted)

  protected:
    static void _bind_methods();

  public:
    godot::PackedStringArray get_fixture_names() const;

    // Build the named committed fixture and hand the derived StructuralWorld
    // to `view` through its C++ production seam.
    bool present(const godot::String& name, FauxBuildView* view);
    godot::String get_last_error() const;

    // Serialize the named committed fixture through the core canonical MAP
    // writer into `directory` (test-infrastructure disk output; the only
    // thing this harness ever writes). Returns the normalized VFS name of
    // the written file (e.g. "SQUARE_ROOM.MAP"), or an empty String with
    // last_error set on failure. The production source route (slice 3)
    // re-loads exactly these bytes through a directory mount.
    godot::String write_fixture_map(const godot::String& name, const godot::String& directory);

    // M6.2B1: serialize the two_sector_portal fixture with AUTHORED texture
    // placement fields set (panning, flips, swap-XY, relative alignment,
    // wall bottom alignment; original synthetic values) as PLACEMENT.MAP.
    // The textured boundary gate loads these bytes so verbatim UV passthrough
    // is proven for placement-carrying UVs, not only default-flag ones.
    godot::String write_placement_map(const godot::String& directory);
    godot::String write_masked_map(const godot::String& directory);

    // Build a synthetic GRP archive in `directory` from committed fixtures,
    // using the approved M4 canonical builder (fauxbuild::synth::build_grp).
    // `entries` is an Array of Dictionaries:
    //
    //     {"fixture": <committed fixture name>,
    //      "name":    <VFS entry name, e.g. "SYNTH.MAP">,
    //      "corrupt": <optional bool; damages the serialized MAP bytes>}
    //
    // The archive names are arbitrary VFS keys: nothing in the route keys
    // behaviour off them. `corrupt` exists so the GRP route can prove it
    // consumes archive bytes rather than re-deriving a fixture by name.
    // Returns the absolute path written, or "" with last_error set.
    godot::String write_fixture_grp(const godot::Array& entries, const godot::String& directory,
                                    const godot::String& file_name);

    // Write ORIGINAL synthetic assets (TILES000.ART + PALETTE.DAT) into
    // `directory`, so the textured production route can be driven end to end
    // by committed content. Test infrastructure only.
    godot::String write_fixture_assets(const godot::String& directory);

    // The EXPECTED side of the textured boundary test: the prepared world's
    // vertices and UVs in canonical prepared order, straight from core. The
    // scene compares these against what the ArrayMesh actually holds, so the
    // view cannot satisfy the test with UVs of its own.
    bool prepare_from_dir(const godot::String& dir_path, const godot::String& map_name);
    godot::PackedVector3Array prepared_vertices() const;
    godot::PackedVector2Array prepared_uvs() const;

    // Present the retained prepared world with every surface's atlas page
    // forced to `page`. Test infrastructure for the transactional page-range
    // regression: it lets the scene drive an INVALID prepared world through
    // the real seam and observe a clean refusal, rather than inferring safety
    // from the absence of a crash.
    bool present_prepared_with_page(FauxBuildView* view, std::int32_t page);

    // The EXPECTED side of the consumer-boundary test: vertices and indices
    // derived directly from the retained StructuralWorld by this harness's
    // own packing — deliberately independent of FauxBuildView's packing, so
    // the scene test compares two implementations against each other and a
    // shared bug cannot cancel out. Kind values follow core SurfaceKind
    // order (Floor=0 .. PortalLower=4).
    std::int32_t expected_surface_count(std::int32_t kind) const;
    godot::PackedVector3Array expected_vertices(std::int32_t kind) const;
    godot::PackedInt32Array expected_indices(std::int32_t kind) const;
    std::int32_t get_note_count() const;
    std::int32_t get_diagnostic_count() const;

  private:
    std::shared_ptr<const fauxbuild::StructuralWorld> world_;
    godot::String last_error_;
    godot::PackedVector3Array prepared_vertices_;
    godot::PackedVector2Array prepared_uvs_;
    std::shared_ptr<const fauxbuild::PreparedWorld> prepared_world_;
};

} // namespace fauxbuild_godot
