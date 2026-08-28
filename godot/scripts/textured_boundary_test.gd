extends Node

# M6.2A textured consumer boundary (D0020). Proves the ARCHITECTURE, not
# historical Build constants: the provisional UV conventions are settled by the
# human visual gate on real content, not asserted here.
#
# The expected side comes from CORE (FauxStructuralFixture.prepare_from_dir ->
# prepared_vertices/prepared_uvs); the actual side is read from
# MeshInstance3D.mesh -> ArrayMesh.surface_get_arrays(). The view is therefore
# unable to satisfy this test with UVs of its own devising.

var failures := 0
var harness: FauxStructuralFixture
var source: FauxStructuralSource
var view: FauxBuildView
var dir := ""


func check(cond: bool, what: String) -> void:
	if not cond:
		failures += 1
		push_error("textured-boundary FAILED: " + what)


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a == "--grp" or a == "--map" or a == "--dir":
			push_error("textured_boundary_test.gd is a CI scene and generates its own "
				+ "synthetic content; it refuses real-content arguments.")
			get_tree().quit(2)
			return

	harness = FauxStructuralFixture.new()
	source = FauxStructuralSource.new()
	view = FauxBuildView.new()
	add_child(view)

	dir = OS.get_temp_dir().path_join("fauxbuild_textured_gate")
	if DirAccess.make_dir_recursive_absolute(dir) != OK:
		check(false, "cannot create scratch dir")
		get_tree().quit(1)
		return

	var map_name := harness.write_fixture_map("two_sector_portal", dir)
	check(map_name != "", "fixture map write failed: " + harness.get_last_error())
	# M6.2B1: the UV boundary gate runs on AUTHORED PLACEMENT content (the
	# same geometry with panning, flips, swap-XY, relative alignment and wall
	# bottom alignment set), so verbatim passthrough is proven for
	# placement-carrying UVs. The plain fixture still feeds the failure-path
	# subtests below.
	var placement_map := harness.write_placement_map(dir)
	check(placement_map != "", "placement map write failed: " + harness.get_last_error())
	# M6.2C1: masked-layer content (masked portal walls with overpicnum 0 AND
	# 1, a masked solid wall, and a non-masked overpicnum carrier) goes
	# through the same consumer boundary.
	var masked_map := harness.write_masked_map(dir)
	check(masked_map != "", "masked map write failed: " + harness.get_last_error())
	check(harness.write_fixture_assets(dir) != "",
		"fixture asset write failed: " + harness.get_last_error())

	if failures == 0:
		_test_uv_boundary(placement_map)
	if failures == 0:
		_test_uv_boundary(masked_map)
	if failures == 0:
		_test_masked_selection(masked_map)
	if failures == 0:
		_test_indexed_upload()
	if failures == 0:
		_test_transactional_failure(map_name)
	if failures == 0:
		_test_page_range_rejection(map_name)

	if failures == 0:
		print("M6.2A textured consumer boundary: OK")
	else:
		push_error("M6.2A textured consumer boundary: %d failure(s)" % failures)
		get_tree().quit(1)


func _mesh_pairs() -> Array:
	# Every (vertex, uv) pair the presentation actually holds, read at the real
	# boundary. Grouping is the view's business; correspondence is not.
	var pairs := []
	for child in view.get_children():
		if not (child is MeshInstance3D) or child.mesh == null:
			continue
		var mesh: ArrayMesh = child.mesh
		for s in range(mesh.get_surface_count()):
			var arrays: Array = mesh.surface_get_arrays(s)
			var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
			var uvs = arrays[Mesh.ARRAY_TEX_UV]
			check(uvs != null, "surface has no UV array at the boundary")
			if uvs == null:
				continue
			check(uvs.size() == verts.size(), "UV count != vertex count at the boundary")
			for i in range(verts.size()):
				pairs.append("%.6f,%.6f,%.6f|%.6f,%.6f"
					% [verts[i].x, verts[i].y, verts[i].z, uvs[i].x, uvs[i].y])
	pairs.sort()
	return pairs


func _test_uv_boundary(map_name: String) -> void:
	# Gates 1, 3 and 5.
	check(source.present_dir_textured(dir, map_name, view),
		"textured presentation failed: " + source.get_last_error())
	check(harness.prepare_from_dir(dir, map_name),
		"core preparation failed: " + harness.get_last_error())

	var want_v: PackedVector3Array = harness.prepared_vertices()
	var want_uv: PackedVector2Array = harness.prepared_uvs()
	check(want_v.size() == want_uv.size(), "core produced unequal vertex/UV counts")
	check(want_v.size() > 0, "core prepared nothing")

	var expected := []
	for i in range(want_v.size()):
		expected.append("%.6f,%.6f,%.6f|%.6f,%.6f"
			% [want_v[i].x, want_v[i].y, want_v[i].z, want_uv[i].x, want_uv[i].y])
	expected.sort()

	var actual := _mesh_pairs()
	check(actual.size() == expected.size(),
		"boundary holds %d (vertex,uv) pairs, core prepared %d" % [actual.size(), expected.size()])
	check(actual == expected,
		"the ArrayMesh UVs are not the prepared UVs verbatim")

	# The UVs must be real, not all-zero: a view that dropped them and wrote
	# zeros would otherwise match a core that did the same.
	var nonzero := 0
	for i in range(want_uv.size()):
		if want_uv[i] != Vector2.ZERO:
			nonzero += 1
	check(nonzero > 0, "every prepared UV is zero; the authority produced nothing")


func _test_masked_selection(map_name: String) -> void:
	# M6.2C1 selection gates at the consumer boundary. The view must receive
	# an already prepared picnum/page/rect: the masked groups sample the
	# OVERPICNUM tiles (0 and 1 on the two portal sides), and no extra group
	# exists for the solid masked wall or the non-masked overpicnum carrier.
	check(source.present_dir_textured(dir, map_name, view),
		"masked presentation failed: " + source.get_last_error())

	var masked_zero := 0
	var masked_one := 0
	var masked_total := 0
	for child in view.get_children():
		if not (child is MeshInstance3D) or child.mesh == null:
			continue
		if not child.name.begins_with("PortalMasked_"):
			continue
		masked_total += 1
		if child.name.begins_with("PortalMasked_0_"):
			masked_zero += 1
		if child.name.begins_with("PortalMasked_1_"):
			masked_one += 1
	check(masked_total == 2,
		"masked portal has two sides, expected exactly 2 PortalMasked groups, got %d"
			% masked_total)
	check(masked_zero == 1,
		"overpicnum 0 is TILE 0: exactly one PortalMasked group must sample tile 0, got %d"
			% masked_zero)
	check(masked_one == 1,
		"exactly one PortalMasked group must sample tile 1 (overpicnum), got %d" % masked_one)

	# The masked groups' UVs are real (the authority produced them), and the
	# verbatim comparison in _test_uv_boundary already covered these surfaces.


func _test_indexed_upload() -> void:
	# Gates 6 and 7: the texture actually uploaded is an R8 indexed page
	# sampled with NEAREST, and a palette LUT accompanies it.
	var checked := 0
	for child in view.get_children():
		if not (child is MeshInstance3D):
			continue
		var material = child.material_override
		check(material is ShaderMaterial, "textured group must use the indexed ShaderMaterial")
		if not (material is ShaderMaterial):
			continue
		var page = material.get_shader_parameter("atlas_page")
		var lut = material.get_shader_parameter("palette_lut")
		check(page is Texture2D, "atlas page texture missing")
		check(lut is Texture2D, "palette LUT texture missing")
		if page is Texture2D:
			var image: Image = page.get_image()
			check(image != null, "atlas page has no image")
			if image != null:
				# R8: one byte per texel. An RGBA atlas would be 4x and would
				# mean some RGBA form had become authoritative.
				check(image.get_format() == Image.FORMAT_R8,
					"atlas page must be FORMAT_R8, got %d" % image.get_format())
				check(image.get_data().size() == image.get_width() * image.get_height(),
					"atlas page is not one byte per texel")
		var code: String = material.shader.code
		check(code.contains("filter_nearest"), "sampling must be nearest, not filtered")
		check(not code.contains("filter_linear"), "linear filtering must not appear")
		check(code.contains("palette_lut"), "shader must map indices through the palette")
		check(code.contains("tile_rect"), "shader must wrap within the tile rect")

		# The atlas sampler carries DATA. `source_color` would ask the renderer
		# to apply an sRGB transfer to palette INDICES, quietly turning them
		# into almost-right ones -- which still looks like a texture, so it
		# would survive a visual inspection. The palette LUT is colour and
		# keeps the hint.
		for line in code.split("\n"):
			if line.contains("atlas_page") and line.contains("uniform"):
				check(not line.contains("source_color"),
					"atlas_page is indexed DATA and must not be source_color: " + line.strip_edges())
				check(line.contains("filter_nearest"),
					"atlas_page must be sampled nearest: " + line.strip_edges())
			if line.contains("palette_lut") and line.contains("uniform"):
				check(line.contains("source_color"),
					"palette_lut IS colour and should keep source_color: " + line.strip_edges())
		checked += 1
	check(checked > 0, "no textured group was inspected")

	# Resource reuse: one texture per PAGE and one shared Shader, not one of
	# each per group. E1L1 is 173 groups over 3 pages; per-group uploads would
	# be 173 copies of the same megabytes.
	var page_textures := {}
	var shaders := {}
	var groups := 0
	for child in view.get_children():
		if not (child is MeshInstance3D):
			continue
		var m = child.material_override
		if not (m is ShaderMaterial):
			continue
		groups += 1
		var tex = m.get_shader_parameter("atlas_page")
		if tex != null:
			page_textures[tex.get_instance_id()] = true
		if m.shader != null:
			shaders[m.shader.get_instance_id()] = true
	check(groups > 1, "need several groups for the sharing check to mean anything")
	check(page_textures.size() < groups,
		"each group uploaded its own atlas page (%d textures for %d groups)"
			% [page_textures.size(), groups])
	check(shaders.size() == 1,
		"the indexed shader must be shared, got %d distinct shaders" % shaders.size())


func _test_transactional_failure(map_name: String) -> void:
	# Gate 10: a failed textured presentation preserves the previous one.
	var before := _mesh_pairs()
	check(before.size() > 0, "nothing presented before the failure probe")
	var groups_before: PackedStringArray = view.get_group_names()

	check(not source.present_dir_textured(dir, "NO_SUCH.MAP", view),
		"missing map must fail")
	check(source.get_last_error() != "", "failure must be structured")

	var after := _mesh_pairs()
	check(after == before, "a failed textured load replaced the previous presentation")
	check(view.get_group_names() == groups_before, "group set changed after a failed load")


func _test_page_range_rejection(map_name: String) -> void:
	# An out-of-range atlas page must be rejected in the INITIAL validation
	# pass. The page index is used only after discard_presentation(), so a
	# check at the point of use would tear down the previous presentation and
	# then read out of bounds — losing the world AND crashing.
	#
	# The probe drives an invalid prepared world through the real seam and
	# requires a clean refusal. Absence of a crash is deliberately NOT the
	# assertion: the test observes the return value and the surviving
	# presentation, so it detects the bad path safely.
	check(source.present_dir_textured(dir, map_name, view),
		"page-range: baseline presentation failed: " + source.get_last_error())
	check(harness.prepare_from_dir(dir, map_name),
		"page-range: core preparation failed: " + harness.get_last_error())

	var before := _mesh_pairs()
	var groups_before: PackedStringArray = view.get_group_names()
	check(before.size() > 0, "page-range: nothing presented before the probe")

	# Below the range, and one past the last valid page. The fixture atlas is
	# single-page, so page_count == 1 and page 1 is the first invalid index.
	for bad_page in [-1, 1]:
		check(not harness.present_prepared_with_page(view, bad_page),
			"page-range: page %d must be rejected" % bad_page)
		check(_mesh_pairs() == before,
			"page-range: rejected page %d replaced the previous presentation" % bad_page)
		check(view.get_group_names() == groups_before,
			"page-range: rejected page %d changed the group set" % bad_page)

	# A valid page still presents, so the check is a range test and not a
	# blanket refusal.
	check(harness.present_prepared_with_page(view, 0),
		"page-range: page 0 must still be accepted")
	check(_mesh_pairs() == before, "page-range: re-presenting page 0 changed the arrays")
