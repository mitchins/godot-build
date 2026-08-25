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
# array-for-array, not count-for-count. What differs for real content is
# only DirectoryMount -> GrpMount; GRP mounting itself is proven by M2.
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
