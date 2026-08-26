#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <cstdint>
#include <memory>

namespace fauxbuild {
class Mount;
}

namespace fauxbuild_godot {

class FauxBuildView;

// Production content entry point (M5 slice 3). The one legitimate
// GDScript-facing owner of the real-content route:
//
//     GRP filesystem path | loose directory   (core mount)
//         -> VFS lookup of the MAP name
//         -> core MAP reader
//         -> core structural derivation -> StructuralWorld
//         -> FauxBuildView.present_world (the slice-2 C++ seam)
//
// This class owns source-side operation only. It never renders anything
// itself: the world always reaches Godot through the view's presentation
// seam, so the view stays a pure consumer and generated meshes stay
// disposable presentation (RENDERING_CONTRACT, D0016). MapData is never
// retained or exposed as script-authoritative state; after a successful
// present, only reporting facts (counts, names, errors) remain.
//
// Failure is transactional: a load that fails anywhere in the chain
// returns false, records a structured last error naming the failed stage,
// leaves the previously presented world untouched (the view is only
// handed a world after the whole chain succeeds), and does not update the
// reporting facts — they keep describing the last successful load.
class FauxStructuralSource : public godot::RefCounted {
    GDCLASS(FauxStructuralSource, godot::RefCounted)

  protected:
    static void _bind_methods();

  public:
    // Source mode A: GRP archive filesystem path + internal MAP VFS name.
    // The archive is mounted in place through the core GRP mount — no
    // extraction, ever.
    bool present_grp(const godot::String& grp_path, const godot::String& map_name,
                     FauxBuildView* view);

    // Source mode B: loose directory filesystem path + MAP VFS name (the
    // name is matched case-insensitively against the flat file list, as in
    // every core mount).
    bool present_dir(const godot::String& dir_path, const godot::String& map_name,
                     FauxBuildView* view);

    // Textured variants (M6 slice 2A). Identical route, plus assets loaded
    // from the SAME VFS the map came from -- never a second mount, never a
    // path the caller supplies separately. Transactional exactly like the
    // untextured path: the view is handed a prepared world only after the
    // whole chain succeeds.
    bool present_grp_textured(const godot::String& grp_path, const godot::String& map_name,
                              FauxBuildView* view);
    bool present_dir_textured(const godot::String& dir_path, const godot::String& map_name,
                              FauxBuildView* view);

    // Reporting facts for the last SUCCESSFUL load (diagnostic state only).
    godot::String get_source_description() const; // e.g. "grp:<path>" / "dir:<path>"
    godot::String get_map_name() const;           // normalized VFS name that was read
    std::int32_t get_sector_count() const;
    std::int32_t get_wall_count() const;
    std::int32_t get_surface_count() const;
    std::int32_t get_triangle_count() const;
    std::int32_t get_note_count() const;
    std::int32_t get_diagnostic_count() const;

    // The map's own parsed start position, converted through the single
    // core transform (to_render_space) like every other vertex. Diagnostic
    // only: the human viewer uses it to aim its initial view at where the
    // map says a player begins, rather than at the centre of the whole
    // shell. The start ANGLE is deliberately not exposed -- Build angle
    // semantics are not M5's, and nothing here needs them.
    godot::Vector3 get_start_position() const;

    // Empty after success; on failure, "<stage>: <structured core error>".
    godot::String get_last_error() const;

  private:
    godot::Vector3 start_position_;
    bool present_from_mount(std::unique_ptr<fauxbuild::Mount> mount, const godot::String& map_name,
                            bool textured, FauxBuildView* view);

    godot::String source_description_;
    godot::String map_name_;
    godot::String last_error_;
    std::int32_t sector_count_ = 0;
    std::int32_t wall_count_ = 0;
    std::int32_t surface_count_ = 0;
    std::int32_t triangle_count_ = 0;
    std::int32_t note_count_ = 0;
    std::int32_t diagnostic_count_ = 0;
};

} // namespace fauxbuild_godot
