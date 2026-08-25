extends Node

# M5 slice-2 consumer-boundary test (the load-bearing one). What is under
# test is what GODOT actually received: every assertion reads
# MeshInstance3D.mesh -> ArrayMesh.surface_get_arrays() — the real boundary
# objects — and compares against the StructuralWorld output provided by the
# FauxStructuralFixture harness (which packs it independently of the view,
# so a shared bug cannot cancel out). No intermediate cache, no helper
# getter standing in for the mesh, no side metadata.

const KIND_FLOOR := 0
const KIND_CEILING := 1
const KIND_SOLID_WALL := 2
const KIND_PORTAL_UPPER := 3
const KIND_PORTAL_LOWER := 4

const GROUPS := ["Floors", "Ceilings", "SolidWalls", "PortalUpper", "PortalLower"]

var failures := 0
var harness: FauxStructuralFixture
var view: FauxBuildView


func check(cond: bool, what: String) -> void:
	if not cond:
		failures += 1
		push_error("structural-boundary FAILED: " + what)


func _ready() -> void:
	# This scene asserts committed-fixture constants unconditionally and can
	# only run against synthetic content. Refuse real-content arguments the
	# way the M4 boundary test refuses --grp (real E1L1 is slice 3).
	for a in OS.get_cmdline_user_args():
		if a == "--grp" or a == "--map" or a == "--dir":
			push_error("structural_view_test.gd is the CI consumer-boundary test and "
				+ "only runs against committed synthetic fixtures. Real content arrives "
				+ "with slice 3.")
			get_tree().quit(2)
			return

	harness = FauxStructuralFixture.new()
	view = FauxBuildView.new()
	add_child(view)

	_test_square_room()
	_test_non_convex_verbatim()
	_test_multi_loop_verbatim()
	_test_portals_and_no_closing_quad()
	_test_asymmetric_transform_probe()
	_test_stale_kind_groups()
	_test_rebuild_round_trip()
	_test_presentation_is_disposable()

	if failures == 0:
		print("M5 structural consumer boundary: OK")
	else:
		push_error("M5 structural consumer boundary: %d failure(s)" % failures)
		get_tree().quit(1)


# Bulk actual-side read. NOTE: the load-bearing direct reads (the asymmetric
# probe and the rebuild round trip) call surface_get_arrays() inline rather
# than through this helper, so tampering with this helper cannot hide a
# wrong mesh (sabotage evidence, slice-2 report).
func actual_arrays(group: String) -> Dictionary:
	var inst := view.get_node_or_null(group)
	if inst == null:
		return {}
	var mesh = inst.mesh
	if mesh == null or not mesh is ArrayMesh or mesh.get_surface_count() == 0:
		check(false, group + ": expected an ArrayMesh with one surface")
		return {}
	var arrays: Array = mesh.surface_get_arrays(0)
	return {"vertices": arrays[Mesh.ARRAY_VERTEX], "indices": arrays[Mesh.ARRAY_INDEX]}


# Compare one kind group, at the boundary, against the StructuralWorld.
func check_group(group: String, kind: int, context: String) -> void:
	var want_v: PackedVector3Array = harness.expected_vertices(kind)
	var want_i: PackedInt32Array = harness.expected_indices(kind)
	if want_v.size() == 0:
		check(view.get_node_or_null(group) == null,
			context + ": " + group + " must have no mesh (kind is empty in the world)")
		return
	var got := actual_arrays(group)
	if got.is_empty():
		check(false, context + ": " + group + " group missing at the boundary")
		return
	check(got.vertices == want_v,
		context + ": " + group + " vertices differ from the StructuralWorld at the Godot boundary")
	check(got.indices == want_i,
		context + ": " + group + " indices differ from the StructuralWorld at the Godot boundary")


func present_fixture(name: String, context: String) -> void:
	check(harness.present(name, view), context + ": present failed: " + harness.get_last_error())
	check(view.has_world(), context + ": view should hold a presented world")


# A. square_room -----------------------------------------------------------

func _test_square_room() -> void:
	present_fixture("square_room", "square_room")
	check(view.get_group_names() == PackedStringArray(["Floors", "Ceilings", "SolidWalls"]),
		"square_room: groups must be exactly Floors/Ceilings/SolidWalls, got "
		+ str(view.get_group_names()))

	# Count anchors from the fixture spec (independent of the harness).
	var floor := actual_arrays("Floors")
	check(floor.vertices.size() == 4, "square_room floor: 4 corners, got %d" % floor.vertices.size())
	check(floor.indices.size() == 6, "square_room floor: 2 triangles = 6 indices, got %d"
		% floor.indices.size())
	var solid := actual_arrays("SolidWalls")
	check(solid.vertices.size() == 16, "square_room walls: 4 x 4 vertices, got %d"
		% solid.vertices.size())
	check(solid.indices.size() == 24, "square_room walls: 4 x 6 indices, got %d"
		% solid.indices.size())

	check_group("Floors", KIND_FLOOR, "square_room")
	check_group("Ceilings", KIND_CEILING, "square_room")
	check_group("SolidWalls", KIND_SOLID_WALL, "square_room")


# B. non_convex ------------------------------------------------------------

func _test_non_convex_verbatim() -> void:
	present_fixture("non_convex", "non_convex")
	# The L-shape floor is exactly 4 triangles; a re-derived fan or convex
	# hull would change counts AND disagree with the StructuralWorld.
	var floor := actual_arrays("Floors")
	check(floor.indices.size() == 12, "non_convex floor: 4 triangles = 12 indices, got %d"
		% floor.indices.size())
	check_group("Floors", KIND_FLOOR, "non_convex")
	check_group("Ceilings", KIND_CEILING, "non_convex")
	check_group("SolidWalls", KIND_SOLID_WALL, "non_convex")


# C. multi_loop ------------------------------------------------------------

func _test_multi_loop_verbatim() -> void:
	present_fixture("multi_loop", "multi_loop")
	# The hole-preserving triangle set must cross verbatim; the hole itself
	# was proven empty by slice 1, so this only proves the boundary copied
	# the accepted triangles without re-deriving them.
	check_group("Floors", KIND_FLOOR, "multi_loop")
	check_group("Ceilings", KIND_CEILING, "multi_loop")
	check_group("SolidWalls", KIND_SOLID_WALL, "multi_loop")


# D. portals ---------------------------------------------------------------

func _test_portals_and_no_closing_quad() -> void:
	present_fixture("two_sector_portal", "two_sector_portal")
	# Equal heights on both sides: no portal spans at all, and no
	# extension-generated full portal-closing quad either (the SolidWalls
	# content comes from the StructuralWorld, compared exactly).
	check(view.get_node_or_null("PortalUpper") == null,
		"two_sector_portal: no PortalUpper group should exist")
	check(view.get_node_or_null("PortalLower") == null,
		"two_sector_portal: no PortalLower group should exist")
	check_group("Floors", KIND_FLOOR, "two_sector_portal")
	check_group("Ceilings", KIND_CEILING, "two_sector_portal")
	check_group("SolidWalls", KIND_SOLID_WALL, "two_sector_portal")

	present_fixture("portal_heights", "portal_heights")
	check(view.get_group_names()
		== PackedStringArray(["Floors", "Ceilings", "SolidWalls", "PortalUpper", "PortalLower"]),
		"portal_heights: all five groups expected, got " + str(view.get_group_names()))
	check_group("PortalUpper", KIND_PORTAL_UPPER, "portal_heights")
	check_group("PortalLower", KIND_PORTAL_LOWER, "portal_heights")
	check_group("SolidWalls", KIND_SOLID_WALL, "portal_heights")


# E. asymmetric transform probe ---------------------------------------------

func _test_asymmetric_transform_probe() -> void:
	present_fixture("asymmetric_probe", "asymmetric_probe")
	var inst := view.get_node_or_null("Floors")
	if inst == null:
		check(false, "asymmetric_probe: Floors group missing")
		return
	# Direct boundary read (NOT via actual_arrays) against constants derived
	# from the committed fixture spec: Build x 1000..11000, y 2000..7000,
	# ceiling z 3000 / floor z 9000, render space (x, -z, y) * 2^-11. Every
	# component is distinct, so a second transform, axis swap, or sign flip
	# fails here even if every helper on the expected side lies. All values
	# are exactly representable in float32, so equality is exact.
	var arrays: Array = inst.mesh.surface_get_arrays(0)
	var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	check(verts.size() == 4, "asymmetric_probe floor: 4 corners, got %d" % verts.size())
	var s := 1.0 / 2048.0
	var want := PackedVector3Array([
		Vector3(1000.0 * s, -9000.0 * s, 2000.0 * s),
		Vector3(11000.0 * s, -9000.0 * s, 2000.0 * s),
		Vector3(11000.0 * s, -9000.0 * s, 7000.0 * s),
		Vector3(1000.0 * s, -9000.0 * s, 7000.0 * s),
	])
	for w in want:
		check(verts.has(w), "asymmetric_probe: floor corner %s not verbatim at the boundary" % w)
	check_group("Floors", KIND_FLOOR, "asymmetric_probe")
	check_group("SolidWalls", KIND_SOLID_WALL, "asymmetric_probe")


# F. absent/empty kinds are not stale ---------------------------------------

func _test_stale_kind_groups() -> void:
	present_fixture("portal_heights", "stale-groups")
	check(view.get_node_or_null("PortalUpper") != null,
		"stale-groups: portal_heights must present a PortalUpper group first")
	present_fixture("square_room", "stale-groups")
	check(view.get_node_or_null("PortalUpper") == null,
		"stale-groups: PortalUpper from the previous world survived the rebuild")
	check(view.get_node_or_null("PortalLower") == null,
		"stale-groups: PortalLower from the previous world survived the rebuild")
	check(view.get_group_names() == PackedStringArray(["Floors", "Ceilings", "SolidWalls"]),
		"stale-groups: groups must be exactly the square_room set, got "
		+ str(view.get_group_names()))


# 9. rebuild round trip: A -> B -> A -----------------------------------------

func _capture_all(fixture: String, context: String) -> Dictionary:
	present_fixture(fixture, context)
	var out := {}
	for group in GROUPS:
		var inst := view.get_node_or_null(group)
		if inst == null:
			continue
		# Direct boundary read: the capture must be what Godot holds, not
		# what any helper says it holds.
		var arrays: Array = inst.mesh.surface_get_arrays(0)
		out[group] = {"vertices": arrays[Mesh.ARRAY_VERTEX], "indices": arrays[Mesh.ARRAY_INDEX]}
	return out


func _test_rebuild_round_trip() -> void:
	var a0 := _capture_all("portal_heights", "round-trip A")
	var b := _capture_all("square_room", "round-trip B")
	var a1 := _capture_all("portal_heights", "round-trip A again")

	check(not a0.is_empty() and not b.is_empty(), "round-trip: captures must not be empty")
	check(a0.has("PortalUpper") and not b.has("PortalUpper"),
		"round-trip: A and B must differ in group composition or the test proves nothing")
	var differ := false
	for group in GROUPS:
		var in_a := a0.has(group)
		var in_b := b.has(group)
		if in_a != in_b:
			differ = true
		elif in_a and in_b:
			if a0[group].vertices != b[group].vertices or a0[group].indices != b[group].indices:
				differ = true
	check(differ, "round-trip: portal_heights and square_room arrays are identical?!")

	for group in a0:
		check(a0[group].vertices == a1[group].vertices,
			"round-trip: %s vertices changed after B -> A rebuild (stale/append state)" % group)
		check(a0[group].indices == a1[group].indices,
			"round-trip: %s indices changed after B -> A rebuild (stale/append state)" % group)


# 9b. presentation is disposable, the world is the authority ------------------

func _test_presentation_is_disposable() -> void:
	present_fixture("portal_heights", "disposable")

	# Damage the generated presentation Godot-side: replace one mesh with an
	# unrelated resource and clear another entirely.
	var floors := view.get_node_or_null("Floors")
	var ceilings := view.get_node_or_null("Ceilings")
	check(floors != null and ceilings != null, "disposable: groups missing before damage")
	if floors == null or ceilings == null:
		return
	floors.mesh = PlaneMesh.new()
	ceilings.mesh = null

	# Re-present the SAME fixture: the view must rebuild from the
	# StructuralWorld, never trust or retain the damaged Godot resources.
	present_fixture("portal_heights", "disposable")
	check_group("Floors", KIND_FLOOR, "disposable (after damage + re-present)")
	check_group("Ceilings", KIND_CEILING, "disposable (after damage + re-present)")
	var probe := actual_arrays("Floors")
	check(probe.vertices.size() > 0, "disposable: Floors mesh must be reconstructed, not empty")
