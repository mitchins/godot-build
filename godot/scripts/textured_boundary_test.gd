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
		_test_cutout(masked_map)
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


func _group_arrays(group_prefix: String) -> Array:
	# All groups whose name starts with the prefix: [verts, uvs, indices] each.
	var out := []
	for child in view.get_children():
		if not (child is MeshInstance3D) or child.mesh == null:
			continue
		if not child.name.begins_with(group_prefix):
			continue
		var arrays: Array = child.mesh.surface_get_arrays(0)
		out.append([child, arrays[Mesh.ARRAY_VERTEX], arrays[Mesh.ARRAY_TEX_UV],
			arrays[Mesh.ARRAY_INDEX]])
	return out


func _first_triangle_normal(verts: PackedVector3Array, indices: PackedInt32Array) -> Vector3:
	var a := verts[indices[0]]
	var b := verts[indices[1]]
	var c := verts[indices[2]]
	return (b - a).cross(c - a)


func _test_masked_selection(map_name: String) -> void:
	# M6.2C1 selection + paired-layer gates at the consumer boundary. The view
	# must receive an already prepared picnum/page/rect: the masked groups
	# sample the OVERPICNUM tiles (0 and 1 on the two portal sides), and no
	# extra group exists for the solid masked wall or the non-masked overpicnum
	# carrier. The paired portal sides are coincident by design with opposite
	# winding and DISTINCT authored placement: both must survive as separate
	# groups with their own UVs, and their presentation must back-face cull
	# (one shared masked shader variant) so the two sides cannot z-fight.
	check(source.present_dir_textured(dir, map_name, view),
		"masked presentation failed: " + source.get_last_error())

	var masked_groups := _group_arrays("PortalMasked_")
	check(masked_groups.size() == 2,
		"paired masked portal has two sides; expected exactly 2 PortalMasked groups "
			+ "(no deduplication), got %d" % masked_groups.size())
	if masked_groups.size() != 2:
		return

	var by_tile := {}
	for entry in masked_groups:
		var child: MeshInstance3D = entry[0]
		if child.name.begins_with("PortalMasked_0_"):
			by_tile[0] = entry
		if child.name.begins_with("PortalMasked_2_"):
			by_tile[2] = entry
	check(by_tile.has(0) and by_tile.has(2),
		"expected one PortalMasked group per overlay tile (0 = the zero case and the "
			+ "no-sentinel case, 2 = gate_cut which carries the sentinel texel)")
	if not (by_tile.has(0) and by_tile.has(2)):
		# Indexing a missing key raises and aborts _ready, which would throw
		# away every failure recorded so far -- the gate must report, not die.
		return

	# Each side keeps its OWN authored UVs (distinct placement per side), and
	# the verbatim comparison in _test_uv_boundary already proved these are
	# the prepared UVs unchanged — so distinct prepared UVs at the boundary
	# prove distinct authored sides survived preparation.
	var uvs_a: PackedVector2Array = by_tile[0][2]
	var uvs_b: PackedVector2Array = by_tile[2][2]
	check(uvs_a.size() == 4 and uvs_b.size() == 4,
		"each masked side is a quad (4 vertices)")
	check(uvs_a != uvs_b,
		"the two portal sides carry DISTINCT authored placement; identical UV arrays "
			+ "mean one side's placement was dropped or shared")

	# Opposite winding: the paired layers are coincident with normals facing
	# their own sectors. The first triangle's normal of each group must point
	# against the other's (dot < 0).
	var n_a := _first_triangle_normal(by_tile[0][1], by_tile[0][3])
	var n_b := _first_triangle_normal(by_tile[2][1], by_tile[2][3])
	check(n_a.dot(n_b) < 0.0,
		"paired masked layers must have OPPOSITE winding (normals dot %f, want < 0)"
			% n_a.dot(n_b))

	# Presentation culling, read from the ACTUAL Shader resources: masked
	# groups back-face cull, ordinary groups stay cull_disabled.
	var masked_shader_ids := {}
	var ordinary_shader_ids := {}
	for child in view.get_children():
		if not (child is MeshInstance3D):
			continue
		var material = child.material_override
		if not (material is ShaderMaterial) or material.shader == null:
			continue
		var code: String = material.shader.code
		if child.name.begins_with("PortalMasked_"):
			check(code.contains("cull_back"),
				"masked presentation must back-face cull (cull_back)")
			check(not code.contains("cull_disabled"),
				"masked presentation must not be cull_disabled (z-fighting)")
			masked_shader_ids[material.shader.get_instance_id()] = true
		else:
			check(code.contains("cull_disabled"),
				"ordinary textured groups keep the accepted cull_disabled behaviour")
			check(not code.contains("cull_back"),
				"ordinary textured groups must not back-face cull")
			ordinary_shader_ids[material.shader.get_instance_id()] = true
	check(masked_shader_ids.size() == 1,
		"all masked groups must share ONE masked shader variant, got %d"
			% masked_shader_ids.size())
	check(ordinary_shader_ids.size() == 1,
		"all ordinary groups must share ONE ordinary shader variant, got %d"
			% ordinary_shader_ids.size())
	check(masked_shader_ids.keys()[0] != ordinary_shader_ids.keys()[0],
		"masked and ordinary groups must not alias to one shader variant")


func _test_cutout(map_name: String) -> void:
	# M6.2C1b. The cutout is a CONSUMER behaviour, so what can be proved here
	# is that the consumer was handed the right facts and spends them the right
	# way: the discard exists, keys on the authoritative R8 INDEX before any
	# palette lookup, uses the world's sentinel rather than a literal of its
	# own, and reaches exactly the surfaces the prepared flag marks. Headless
	# Godot rasterizes nothing, so no pixel assertion is possible or claimed.
	check(source.present_dir_textured(dir, map_name, view),
		"cutout presentation failed: " + source.get_last_error())

	var masked_code := ""
	var ordinary_code := ""
	var masked_sentinels := {}
	for child in view.get_children():
		if not (child is MeshInstance3D):
			continue
		var material = child.material_override
		if not (material is ShaderMaterial) or material.shader == null:
			continue
		if child.name.begins_with("PortalMasked_"):
			masked_code = material.shader.code
			masked_sentinels[material.get_shader_parameter("transparent_index")] = true
		else:
			ordinary_code = material.shader.code
			# The ordinary variant must not merely lack a sentinel value — it
			# must have no discard at all, or a tile shared with a masked
			# layer would cut out on an ordinary wall too.
			check(not ordinary_code.contains("discard"),
				"ordinary surfaces must stay OPAQUE: the ordinary shader must not discard")

	check(masked_code != "", "no masked group was presented; the cutout gate proves nothing")
	check(masked_code.contains("discard"),
		"the masked shader must discard sentinel texels")
	check(masked_code.contains("transparent_index"),
		"the discard must compare against the prepared world's transparent_index, "
			+ "never a literal the view holds itself")
	# ...and compare against it EXACTLY. "transparent_index" appearing somewhere
	# in the condition is not enough: `index == transparent_index - 1` would
	# satisfy that while silently cutting the sentinel's NEIGHBOUR instead,
	# which is invisible in every other assertion here.
	check(masked_code.contains("if (index == transparent_index) {"),
		"the discard condition must be exactly `index == transparent_index`; an offset "
			+ "or inequality would cut out a neighbouring palette index")
	# The decision must be made on the INDEX, before the palette lookup: two
	# palette entries share the sentinel's exact RGB, so a colour test would
	# wrongly discard the other one.
	var discard_at := masked_code.find("discard")
	var lookup_at := masked_code.find("palette_lut, vec2")
	check(discard_at >= 0 and lookup_at >= 0 and discard_at < lookup_at,
		"the cutout decision must be made on the R8 index BEFORE the palette lookup")
	check(masked_code.contains("int index = int(round(raw * 255.0))"),
		"the index must be recovered exactly from the R8 sample, so a neighbouring "
			+ "index cannot fall into the sentinel comparison")
	for banned in ["ALBEDO.r ==", "rgb ==", "distance(", "vec3(1.0, 0.0, 1.0)"]:
		check(not masked_code.contains(banned),
			"the cutout must not be decided from palette RGB: found '%s'" % banned)

	# The sentinel actually delivered to the material is the prepared world's,
	# and there is exactly one of it.
	check(masked_sentinels.size() == 1,
		"all masked groups must receive ONE sentinel value, got %d" % masked_sentinels.size())
	check(masked_sentinels.has(255),
		"the ratified sentinel for this palette profile is index 255, got %s"
			% str(masked_sentinels.keys()))

	# Selection follows the PREPARED FLAG, not the tile: gate_cut (tile 2) is
	# presented BOTH as a masked overlay and as an ordinary solid wall in this
	# fixture, and only the masked use may carry the cutout shader.
	var cut_masked := 0
	var cut_ordinary := 0
	for child in view.get_children():
		if not (child is MeshInstance3D):
			continue
		var material = child.material_override
		if not (material is ShaderMaterial) or material.shader == null:
			continue
		if not child.name.contains("_2_"):
			continue
		if child.name.begins_with("PortalMasked_"):
			cut_masked += 1
			check(material.shader.code.contains("discard"),
				"the masked use of the shared tile must cut out")
		else:
			cut_ordinary += 1
			check(not material.shader.code.contains("discard"),
				"the ORDINARY use of the very same tile must stay opaque")
	check(cut_masked == 1, "expected the sentinel-bearing tile as one masked layer, got %d"
		% cut_masked)
	check(cut_ordinary >= 1,
		"expected the same tile on at least one ordinary surface (the control), got %d"
			% cut_ordinary)


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

	# Resource reuse: one texture per PAGE and ONE shared Shader per VARIANT
	# (ordinary + masked, M6.2C1), not one of each per group. E1L1 is 173
	# groups over 3 pages; per-group uploads would be 173 copies of the same
	# megabytes, and per-group shaders would be 173 compilations of two codes.
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
	# The current content has masked groups, so BOTH variants are live: the
	# bound is exactly two, never per group.
	check(shaders.size() == 2,
		"expected exactly the two shared indexed shader variants (ordinary + masked), "
			+ "got %d" % shaders.size())


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
