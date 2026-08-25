extends Node

# M5 slice-3 production-route test. Proves that serialization + mount +
# parse + the production source owner change nothing before the view the
# slices 1-2 boundary already proved:
#
#   DIRECT:      synth MapData -> build_structural_world -> FauxBuildView
#   PRODUCTION:  synth MapData -> canonical write_map -> DirectoryMount
#                  -> Vfs -> MAP parser -> build_structural_world
#                  -> FauxStructuralSource -> FauxBuildView
#
# Both routes are read at the ACTUAL Godot boundary
# (MeshInstance3D.mesh -> ArrayMesh.surface_get_arrays()) and compared
# array-for-array, not count-for-count.
#
# The GRP cases below close the last real-content-only gap: the approved
# M4 canonical builder (synth::build_grp) packs a serialized fixture into
# a scratch archive, so present_grp's SUCCESS path -- the one the human
# E1L1 gate uses -- runs in CI on original synthetic content. Real content
# then differs from CI in no code path at all, only in which bytes are
# mounted.
#
# The standing corruption case at the end is the route-integrity
# tripwire: it corrupts the serialized MAP bytes after writing and proves
# the production route observes the corruption while the direct fixture
# route does not — so this test cannot silently stop consuming the
# mounted file bytes.

const GROUPS := ["Floors", "Ceilings", "SolidWalls", "PortalUpper", "PortalLower"]
const KINDS := [0, 1, 2, 3, 4]

# Sector/wall counts from the committed core fixture specs (map_synth).
const FIXTURE_FACTS := {
	"square_room": {"sectors": 1, "walls": 4},
	"non_convex": {"sectors": 1, "walls": 6},
	"multi_loop": {"sectors": 1, "walls": 8},
	"two_sector_portal": {"sectors": 2, "walls": 8},
	"portal_heights": {"sectors": 2, "walls": 8},
}

var failures := 0
var harness: FauxStructuralFixture
var source: FauxStructuralSource
var view: FauxBuildView
var dir := ""


func check(cond: bool, what: String) -> void:
	if not cond:
		failures += 1
		push_error("source-route FAILED: " + what)


func _ready() -> void:
	# This scene generates its own synthetic fixtures and can only run
	# against them. Real content is the human viewer's job.
	for a in OS.get_cmdline_user_args():
		if a == "--grp" or a == "--map" or a == "--dir":
			push_error("structural_source_test.gd is the CI production-route test; it "
				+ "generates its own synthetic fixtures and refuses real-content arguments.")
			get_tree().quit(2)
			return

	harness = FauxStructuralFixture.new()
	source = FauxStructuralSource.new()
	view = FauxBuildView.new()
	add_child(view)

	dir = _setup_scratch_dir()
	check(dir != "", "scratch directory could not be prepared")

	if failures == 0:
		for fixture in ["square_room", "non_convex", "multi_loop", "two_sector_portal",
				"portal_heights"]:
			_test_route_equivalence(fixture)
	if failures == 0:
		_test_error_paths()
		_test_corrupted_serialized_map()
	if failures == 0:
		_test_grp_route("portal_heights")
		_test_corrupted_grp_map()
		_test_grp_requested_entry()

	if failures == 0:
		print("M5 production source route: OK")
	else:
		push_error("M5 production source route: %d failure(s)" % failures)
		get_tree().quit(1)


func _setup_scratch_dir() -> String:
	# Outside the repository, wiped per run: a gate must never mutate the
	# tree it checks (M4 lesson). The directory is flat by construction, so
	# cleaning means removing its regular files.
	var root := OS.get_temp_dir().path_join("fauxbuild_structural_source_test")
	var made := DirAccess.make_dir_recursive_absolute(root)
	if made != OK:
		push_error("source-route: cannot create scratch dir " + root)
		return ""
	var da := DirAccess.open(root)
	if da == null:
		push_error("source-route: cannot open scratch dir " + root)
		return ""
	da.list_dir_begin()
	var entry := da.get_next()
	while entry != "":
		if not da.current_is_dir() and entry != "." and entry != "..":
			var err := da.remove(entry)
			if err != OK:
				push_error("source-route: cannot clean scratch entry " + entry)
				return ""
		entry = da.get_next()
	da.list_dir_end()
	return root


# Direct boundary read of every group the view currently presents. No
# helper caches: each call re-reads MeshInstance3D.mesh.
func capture(context: String) -> Dictionary:
	var out := {}
	for group in GROUPS:
		var inst := view.get_node_or_null(group)
		if inst == null:
			continue
		var mesh = inst.mesh
		if mesh == null or not mesh is ArrayMesh or mesh.get_surface_count() == 0:
			check(false, context + ": " + group + " has no ArrayMesh surface at the boundary")
			continue
		var arrays: Array = mesh.surface_get_arrays(0)
		out[group] = {"vertices": arrays[Mesh.ARRAY_VERTEX], "indices": arrays[Mesh.ARRAY_INDEX]}
	return out


func _test_route_equivalence(name: String) -> void:
	var facts: Dictionary = FIXTURE_FACTS[name]

	# Direct route (the already-proven slice-2 path).
	check(harness.present(name, view),
		name + ": direct route failed: " + harness.get_last_error())
	var direct := capture(name)
	var direct_groups: PackedStringArray = view.get_group_names()
	var direct_notes := harness.get_note_count()
	var direct_diags := harness.get_diagnostic_count()
	var direct_surfaces := 0
	for k in KINDS:
		direct_surfaces += harness.expected_surface_count(k)

	# Serialized production route through the production source owner.
	var vfs_name := harness.write_fixture_map(name, dir)
	check(vfs_name != "",
		name + ": fixture MAP write failed: " + harness.get_last_error())
	if vfs_name == "":
		return
	check(source.present_dir(dir, vfs_name, view),
		name + ": production route failed: " + source.get_last_error())
	var prod := capture(name)

	check(not direct.is_empty(), name + ": direct capture empty")

	# Group presence and the actual arrays must agree between the routes.
	check(view.get_group_names() == direct_groups,
		name + ": group names differ between routes: "
		+ str(view.get_group_names()) + " vs " + str(direct_groups))
	for group in GROUPS:
		check(direct.has(group) == prod.has(group),
			name + ": " + group + " presence differs between routes")
		if direct.has(group) and prod.has(group):
			check(prod[group].vertices == direct[group].vertices,
				name + ": " + group + " vertices differ between direct and production routes")
			check(prod[group].indices == direct[group].indices,
				name + ": " + group + " indices differ between direct and production routes")

	# Source facts agree with the direct route and the fixture spec.
	check(source.get_last_error() == "", name + ": unexpected last_error after success")
	check(source.get_source_description().begins_with("dir:"),
		name + ": source description should name the directory mount, got "
		+ source.get_source_description())
	check(source.get_map_name() == vfs_name,
		name + ": resolved map name should be the written VFS name")
	check(source.get_sector_count() == facts.sectors,
		name + ": sectors %d != fixture spec %d" % [source.get_sector_count(), facts.sectors])
	check(source.get_wall_count() == facts.walls,
		name + ": walls %d != fixture spec %d" % [source.get_wall_count(), facts.walls])
	check(source.get_surface_count() == direct_surfaces,
		name + ": surfaces %d != direct route %d"
		% [source.get_surface_count(), direct_surfaces])
	var triangles := 0
	for group in prod:
		triangles += prod[group].indices.size() / 3
	check(source.get_triangle_count() == triangles,
		name + ": triangles %d != boundary-derived %d"
		% [source.get_triangle_count(), triangles])
	check(source.get_note_count() == direct_notes,
		name + ": notes %d != direct route %d" % [source.get_note_count(), direct_notes])
	check(source.get_diagnostic_count() == direct_diags,
		name + ": diagnostics %d != direct route %d"
		% [source.get_diagnostic_count(), direct_diags])


func _test_error_paths() -> void:
	# Missing MAP inside a valid directory mount.
	check(not source.present_dir(dir, "NO_SUCH.MAP", view),
		"missing-map: load must fail")
	check(source.get_last_error() != "", "missing-map: structured error expected")
	check("vfs lookup" in source.get_last_error(),
		"missing-map: error should name the vfs stage, got: " + source.get_last_error())

	# Malformed MAP bytes inside a valid directory mount.
	var bad := dir.path_join("BAD.MAP")
	var f := FileAccess.open(bad, FileAccess.WRITE)
	check(f != null, "malformed: cannot write probe map")
	if f != null:
		f.store_32(99) # version 99 != 7
		f.close()
	check(not source.present_dir(dir, "BAD.MAP", view),
		"malformed: load must fail")
	check("map parse" in source.get_last_error(),
		"malformed: error should name the parse stage, got: " + source.get_last_error())

	# Nonexistent directory.
	check(not source.present_dir("/nonexistent/fauxbuild/source/dir", "X.MAP", view),
		"missing-dir: load must fail")
	check("directory mount" in source.get_last_error(),
		"missing-dir: error should name the mount stage, got: " + source.get_last_error())

	# Nonexistent GRP path (no proprietary data needed for a miss).
	check(not source.present_grp("/nonexistent/fauxbuild/archive.grp", "X.MAP", view),
		"missing-grp: load must fail")
	check("grp mount" in source.get_last_error(),
		"missing-grp: error should name the mount stage, got: " + source.get_last_error())

	# Null view is a caller bug, not a crash.
	check(not source.present_dir(dir, "BAD.MAP", null), "null-view: load must fail")
	check(source.get_last_error() != "", "null-view: structured error expected")


func _test_corrupted_serialized_map() -> void:
	# Route-integrity tripwire (standing): the production route must
	# consume the serialized MAP bytes, and a failed load must not
	# replace a previously valid presentation.
	var vfs_name := harness.write_fixture_map("portal_heights", dir)
	check(vfs_name == "PORTAL_HEIGHTS.MAP", "corrupt: expected written name PORTAL_HEIGHTS.MAP")
	check(source.present_dir(dir, vfs_name, view),
		"corrupt: pre-corruption load failed: " + source.get_last_error())
	var good := capture("corrupt")
	check(good.has("PortalUpper") and good.has("PortalLower"),
		"corrupt: portal_heights must present portal groups before corruption")
	var good_surfaces := source.get_surface_count()
	var good_description := source.get_source_description()

	# Corrupt the serialized bytes on disk (version field -> 99).
	var path := dir.path_join(vfs_name)
	var f := FileAccess.open(path, FileAccess.READ_WRITE)
	check(f != null, "corrupt: cannot open written map for corruption")
	if f != null:
		f.seek(0)
		f.store_8(99)
		f.close()

	check(not source.present_dir(dir, vfs_name, view),
		"corrupt: production route must observe the corrupted MAP bytes")
	check("map parse" in source.get_last_error(),
		"corrupt: error should name the parse stage, got: " + source.get_last_error())

	# No half-built replacement: the previous presentation is untouched.
	var still := capture("corrupt")
	check(still.size() == good.size(), "corrupt: group set changed after failed load")
	for group in good:
		check(still.has(group), "corrupt: " + group + " disappeared after failed load")
		if still.has(group):
			check(still[group].vertices == good[group].vertices and
				still[group].indices == good[group].indices,
				"corrupt: failed load replaced the previous presentation: " + group)

	# Reporting facts are those of the last SUCCESS (transactional).
	check(source.get_surface_count() == good_surfaces,
		"corrupt: facts must keep describing the last successful load")
	check(source.get_source_description() == good_description,
		"corrupt: source description must survive a failed load")

	# The direct route does not read those bytes: the routes are provably
	# distinct consumers of different inputs.
	check(harness.present("portal_heights", view),
		"corrupt: direct fixture route must be unaffected by serialized-byte corruption")
	var direct_after := capture("corrupt")
	for group in good:
		check(direct_after.has(group) and direct_after[group].vertices == good[group].vertices,
			"corrupt: direct route arrays should still match after byte corruption: " + group)


func _test_grp_route(name: String) -> void:
	# present_grp SUCCESS through the production owner. The archive entry
	# name is an arbitrary VFS key: no behaviour anywhere keys off it.
	var facts: Dictionary = FIXTURE_FACTS[name]
	var entry_name := "SYNTH.MAP"

	# Reference: the DirectoryMount production route, itself already proven
	# array-equal to the direct fixture route above.
	var vfs_name := harness.write_fixture_map(name, dir)
	check(vfs_name != "", "grp: reference MAP write failed: " + harness.get_last_error())
	if vfs_name == "":
		return
	check(source.present_dir(dir, vfs_name, view),
		"grp: reference dir route failed: " + source.get_last_error())
	var reference := capture("grp-reference")
	var reference_groups: PackedStringArray = view.get_group_names()
	var reference_sectors := source.get_sector_count()
	var reference_walls := source.get_wall_count()
	var reference_surfaces := source.get_surface_count()
	var reference_triangles := source.get_triangle_count()
	var reference_notes := source.get_note_count()
	var reference_diags := source.get_diagnostic_count()

	# The same fixture, packed into a synthetic GRP by the approved M4
	# builder, loaded through the production GRP entry point.
	var grp_path := harness.write_fixture_grp(
		[{"fixture": name, "name": entry_name}], dir, "SYNTH.GRP")
	check(grp_path != "", "grp: archive write failed: " + harness.get_last_error())
	if grp_path == "":
		return
	check(source.present_grp(grp_path, entry_name, view),
		"grp: production GRP route failed: " + source.get_last_error())
	var packed := capture("grp")

	check(source.get_last_error() == "", "grp: unexpected last_error after success")
	check(source.get_source_description().begins_with("grp:"),
		"grp: source description should name the GRP mount, got "
		+ source.get_source_description())
	check(source.get_map_name() == entry_name,
		"grp: resolved map name should be the requested VFS entry, got "
		+ source.get_map_name())

	# Facts agree with the reference route AND the committed fixture spec.
	check(source.get_sector_count() == reference_sectors,
		"grp: sectors %d != dir route %d" % [source.get_sector_count(), reference_sectors])
	check(source.get_sector_count() == facts.sectors,
		"grp: sectors %d != fixture spec %d" % [source.get_sector_count(), facts.sectors])
	check(source.get_wall_count() == reference_walls,
		"grp: walls %d != dir route %d" % [source.get_wall_count(), reference_walls])
	check(source.get_wall_count() == facts.walls,
		"grp: walls %d != fixture spec %d" % [source.get_wall_count(), facts.walls])
	check(source.get_surface_count() == reference_surfaces,
		"grp: surfaces %d != dir route %d"
		% [source.get_surface_count(), reference_surfaces])
	check(source.get_triangle_count() == reference_triangles,
		"grp: triangles %d != dir route %d"
		% [source.get_triangle_count(), reference_triangles])
	check(source.get_note_count() == reference_notes,
		"grp: notes %d != dir route %d" % [source.get_note_count(), reference_notes])
	check(source.get_diagnostic_count() == reference_diags,
		"grp: diagnostics %d != dir route %d"
		% [source.get_diagnostic_count(), reference_diags])

	# Triangle count must also agree with what the boundary actually holds.
	var boundary_triangles := 0
	for group in packed:
		boundary_triangles += packed[group].indices.size() / 3
	check(source.get_triangle_count() == boundary_triangles,
		"grp: triangles %d != boundary-derived %d"
		% [source.get_triangle_count(), boundary_triangles])

	# Group presence and the ACTUAL ArrayMesh arrays.
	check(view.get_group_names() == reference_groups,
		"grp: group names differ between mounts: "
		+ str(view.get_group_names()) + " vs " + str(reference_groups))
	check(not reference.is_empty(), "grp: reference capture empty")
	for group in GROUPS:
		check(reference.has(group) == packed.has(group),
			"grp: " + group + " presence differs between dir and grp mounts")
		if reference.has(group) and packed.has(group):
			check(packed[group].vertices == reference[group].vertices,
				"grp: " + group + " vertices differ between dir and grp mounts")
			check(packed[group].indices == reference[group].indices,
				"grp: " + group + " indices differ between dir and grp mounts")


func _test_corrupted_grp_map() -> void:
	# GRP route-integrity tripwire. Equivalence alone cannot distinguish
	# consuming archive bytes from re-deriving a fixture by filename, so
	# the GRP path gets its own corruption proof: a well-formed archive
	# whose MAP payload is damaged must fail at the PARSE stage.
	var good_path := harness.write_fixture_grp(
		[{"fixture": "portal_heights", "name": "SYNTH.MAP"}], dir, "GOOD.GRP")
	check(good_path != "", "grp-corrupt: good archive write failed: " + harness.get_last_error())
	check(source.present_grp(good_path, "SYNTH.MAP", view),
		"grp-corrupt: pre-corruption load failed: " + source.get_last_error())
	var good := capture("grp-corrupt")
	check(good.has("PortalUpper") and good.has("PortalLower"),
		"grp-corrupt: portal_heights must present portal groups before corruption")
	var good_surfaces := source.get_surface_count()
	var good_description := source.get_source_description()

	var bad_path := harness.write_fixture_grp(
		[{"fixture": "portal_heights", "name": "SYNTH.MAP", "corrupt": true}], dir, "BAD.GRP")
	check(bad_path != "", "grp-corrupt: bad archive write failed: " + harness.get_last_error())
	check(not source.present_grp(bad_path, "SYNTH.MAP", view),
		"grp-corrupt: production route must observe the corrupted MAP bytes inside the archive")
	check("map parse" in source.get_last_error(),
		"grp-corrupt: error should name the parse stage, got: " + source.get_last_error())

	# A failed GRP load replaces nothing and rewrites no facts.
	var still := capture("grp-corrupt")
	check(still.size() == good.size(), "grp-corrupt: group set changed after failed load")
	for group in good:
		check(still.has(group), "grp-corrupt: " + group + " disappeared after failed load")
		if still.has(group):
			check(still[group].vertices == good[group].vertices and
				still[group].indices == good[group].indices,
				"grp-corrupt: failed load replaced the previous presentation: " + group)
	check(source.get_surface_count() == good_surfaces,
		"grp-corrupt: facts must keep describing the last successful load")
	check(source.get_source_description() == good_description,
		"grp-corrupt: source description must survive a failed load")


func _test_grp_requested_entry() -> void:
	# Two valid MAP entries in one archive: the REQUESTED VFS name must be
	# the one loaded. portal_heights and square_room differ in sector/wall
	# counts and in whether portal groups exist at all, so a wrong-entry
	# load cannot pass by coincidence.
	var path := harness.write_fixture_grp([
		{"fixture": "square_room", "name": "OTHER.MAP"},
		{"fixture": "portal_heights", "name": "SYNTH.MAP"},
	], dir, "TWO.GRP")
	check(path != "", "grp-entry: archive write failed: " + harness.get_last_error())
	if path == "":
		return

	check(source.present_grp(path, "SYNTH.MAP", view),
		"grp-entry: requested entry failed to load: " + source.get_last_error())
	check(source.get_map_name() == "SYNTH.MAP",
		"grp-entry: resolved name should be SYNTH.MAP, got " + source.get_map_name())
	check(source.get_sector_count() == FIXTURE_FACTS["portal_heights"].sectors,
		"grp-entry: SYNTH.MAP should load portal_heights, got %d sectors"
		% source.get_sector_count())
	check(source.get_wall_count() == FIXTURE_FACTS["portal_heights"].walls,
		"grp-entry: SYNTH.MAP should load portal_heights, got %d walls"
		% source.get_wall_count())
	var portal_groups := capture("grp-entry")
	check(portal_groups.has("PortalUpper"),
		"grp-entry: portal_heights must present PortalUpper; wrong entry loaded?")

	# The other entry, from the same archive, must load its own content.
	check(source.present_grp(path, "OTHER.MAP", view),
		"grp-entry: second entry failed to load: " + source.get_last_error())
	check(source.get_map_name() == "OTHER.MAP",
		"grp-entry: resolved name should be OTHER.MAP, got " + source.get_map_name())
	check(source.get_sector_count() == FIXTURE_FACTS["square_room"].sectors,
		"grp-entry: OTHER.MAP should load square_room, got %d sectors"
		% source.get_sector_count())
	var square_groups := capture("grp-entry")
	check(not square_groups.has("PortalUpper"),
		"grp-entry: square_room has no portals; the previous entry leaked through")
