extends Node

# M4 slice 4 consumer-boundary test (the load-bearing one). The expected
# values below are re-derived from the committed fixture spec
# (fixtures/atlas/generate.py) — they are NOT read back from the atlas.
# What is under test is what GODOT actually receives through the
# GDExtension API: raw index bytes, rect metadata, image representations,
# and the palette-derived preview math.

const PAGE_W := 2048
const PAGE_H := 2048

# Fixture index formulas (asymmetric in x/y so transposition is loud).
static func index_a(x: int, y: int) -> int:
	return (17 + 29 * x + 53 * y) & 0xFF

static func index_b(x: int, y: int) -> int:
	return (0x80 + 3 * x + 5 * y) & 0xFF

static func index_d(x: int, y: int) -> int:
	return (0xC0 + x + y) & 0xFF

static func index_e(x: int, y: int) -> int:
	return (0x30 + 17 * x + y) & 0xFF

# Fixture palette formulas (6-bit components).
static func pal_component(byte_index: int) -> int:
	return (byte_index * 7) & 0x3F

static func alt_component(byte_index: int) -> int:
	return (byte_index * 5) & 0x3F

var failures := 0

func check(cond: bool, what: String) -> void:
	if not cond:
		failures += 1
		push_error("consumer-boundary FAILED: " + what)

func six_to_eight(v: int) -> int:
	return (v * 255 + 31) / 63

func _ready() -> void:
	var dir := ProjectSettings.globalize_path("res://").path_join("../fixtures/atlas")
	var grp := ""
	var args := OS.get_cmdline_user_args()
	var i := 0
	while i < args.size():
		if args[i] == "--grp" and i + 1 < args.size():
			grp = args[i + 1]
			i += 2
		elif args[i] == "--dir" and i + 1 < args.size():
			dir = args[i + 1]
			i += 2
		else:
			i += 1

	var assets := FauxAssetSet.new()
	var ok := false
	if not grp.is_empty():
		ok = assets.load_grp(grp)
	else:
		ok = assets.load_dir(dir)
	if not ok:
		push_error("FauxAssetSet load failed: " + assets.get_last_error())
		get_tree().quit(1)
		return

	_assert_atlas(assets)
	_assert_images(assets)
	_assert_preview_math(assets)
	if grp.is_empty():
		_assert_multipage(dir)

	var preview := FauxAtlasPreview.new()
	preview.asset = assets
	add_child(preview)
	check(preview.is_ready(), "FauxAtlasPreview failed to build (see errors above)")

	if failures == 0:
		print("M4 consumer boundary: OK")
		print("M4 atlas preview: OK")
	else:
		push_error("M4 consumer boundary: %d failure(s)" % failures)
		get_tree().quit(1)

# The authoritative indexed payload + rect metadata as Godot receives them.
func _assert_atlas(a: FauxAssetSet) -> void:
	check(a.get_tile_count() == 11, "tile count should be 11, got %d" % a.get_tile_count())
	check(a.get_page_count() == 1, "page count should be 1")
	check(a.get_page_size() == Vector2i(PAGE_W, PAGE_H), "unexpected page size")

	var pixels := a.get_pixels()
	# The byte-count tripwire: one index per texel, never four.
	check(pixels.size() == PAGE_W * PAGE_H * 1,
		"indexed byte count %d != %d (RGBA leak?)" % [pixels.size(), PAGE_W * PAGE_H])

	var r1: Rect2i = a.get_tile_rect(1)
	check(r1.size == Vector2i(8, 8), "tile 1 size should be 8x8")
	check(r1.position.x >= 0 and r1.position.y >= 0
		and r1.position.x + 8 <= PAGE_W and r1.position.y + 8 <= PAGE_H,
		"tile 1 rect out of page bounds")
	var saw_asymmetry := false
	for px in range(8):
		for py in range(8):
			var at := (r1.position.y + py) * PAGE_W + r1.position.x + px
			check(pixels[at] == index_a(px, py),
				"tile 1 byte at (%d,%d): got %d want %d"
				% [px, py, pixels[at], index_a(px, py)])
			saw_asymmetry = saw_asymmetry or index_a(px, py) != index_a(py, px)
	check(saw_asymmetry, "fixture must be transpose-detecting")

	var r2: Rect2i = a.get_tile_rect(2)
	check(r2.size == Vector2i(5, 3), "tile 2 (non-square) size should be 5x3")
	for px in range(5):
		for py in range(3):
			var at2 := (r2.position.y + py) * PAGE_W + r2.position.x + px
			check(pixels[at2] == index_b(px, py),
				"tile 2 byte at (%d,%d): got %d want %d"
				% [px, py, pixels[at2], index_b(px, py)])

	var r8: Rect2i = a.get_tile_rect(8)
	check(r8.size == Vector2i(6, 2), "tile 8 size should be 6x2")
	check(pixels[r8.position.y * PAGE_W + r8.position.x] == index_d(0, 0), "tile 8 (0,0)")
	var r9: Rect2i = a.get_tile_rect(9)
	check(r9.size == Vector2i(2, 6), "tile 9 size should be 2x6")
	check(pixels[(r9.position.y + 5) * PAGE_W + r9.position.x + 1] == index_e(1, 5),
		"tile 9 (1,5)")

	# Explicit empty entries: gap picnums 4..7, zero-dim 0 and 10.
	for p in [4, 5, 6, 7, 0, 10]:
		var m: Dictionary = a.get_tile_meta(p)
		check(not m["populated"], "picnum %d should be an explicit empty entry" % p)
		check(m["page"] == -1, "picnum %d should have no page" % p)
		check(a.get_tile_rect(p).size == Vector2i(0, 0), "picnum %d rect should be empty" % p)

	# Animation anchor metadata (picanm preserved through the pipeline).
	var m3: Dictionary = a.get_tile_meta(3)
	check(m3["frames"] == 3, "anchor frames should be 3")
	check(m3["anim_type"] == 2, "anchor anim_type should be forward(2)")
	check(m3["speed"] == 5, "anchor speed should be 5")
	check(m3["x_center"] == -2 and m3["y_center"] == 3, "anchor pivot should be (-2,3)")
	check(a.get_tile_pivot(3) == Vector2i(-2, 3), "pivot accessor should agree")
	# picanm_raw(3, 2, -2, 3, 5) from the fixture spec.
	check(m3["raw"] == (5 << 24) | ((3 & 0xFF) << 16) | ((-2 & 0xFF) << 8) | (2 << 6) | 3,
		"raw picanm dword should be preserved verbatim")

	# Row-major per-tile accessor agrees with the page bytes.
	var t2 := a.get_tile_indices(2)
	check(t2.size() == 15, "tile 2 accessor should return 15 bytes")
	check(t2[0] == index_b(0, 0) and t2[14] == index_b(4, 2), "tile 2 accessor corners")

# Multi-page, at the boundary. The default fixture is one page, so page
# rebinding was reachable only by a human running gate A over real content --
# a bug found there and fixed with a one-page test learns nothing. Small pages
# force ordinary fixture tiles onto page 1 with no proprietary content.
func _assert_multipage(dir: String) -> void:
	const SMALL := 12
	var multi := FauxAssetSet.new()
	if not multi.load_dir(dir, SMALL, SMALL):
		check(false, "small-page load failed: " + multi.get_last_error())
		return
	check(multi.get_page_size() == Vector2i(SMALL, SMALL), "page size override ignored")
	var pages := multi.get_page_count()
	check(pages > 1, "small pages must produce more than one page, got %d" % pages)

	var bytes: PackedByteArray = multi.get_pixels()
	check(bytes.size() == SMALL * SMALL * pages,
		"indexed byte count %d != %d" % [bytes.size(), SMALL * SMALL * pages])

	# Find a populated tile on a page above 0 and verify its texels are read
	# from *its own* page, which is exactly what binding page 0 for everything
	# would get wrong.
	var checked_above_zero := false
	for p in range(multi.get_tile_count()):
		var m: Dictionary = multi.get_tile_meta(p)
		if m["page"] <= 0:
			continue
		var page: int = m["page"]
		var rect: Rect2i = multi.get_tile_rect(p)
		var size: Vector2i = multi.get_tile_size(p)
		if size.x <= 0 or size.y <= 0:
			continue
		check(rect.position.x + size.x <= SMALL and rect.position.y + size.y <= SMALL,
			"tile %d rect escapes its page" % p)

		# Every page has its own Image; the tile must match the one it claims.
		var img = multi.make_index_image(page)
		check(img != null, "page %d image missing" % page)
		if img == null:
			continue
		check(img.get_format() == Image.FORMAT_R8, "page %d not R8" % page)
		var data: PackedByteArray = img.get_data()
		var per_tile: PackedByteArray = multi.get_tile_indices(p)
		var wrong_page := 0
		var mismatched := 0
		var page0: PackedByteArray = multi.make_index_image(0).get_data()
		for py in range(size.y):
			for px in range(size.x):
				var at := (rect.position.y + py) * SMALL + rect.position.x + px
				var want := per_tile[py * size.x + px]
				if data[at] != want:
					mismatched += 1
				if page0[at] != want:
					wrong_page += 1
		check(mismatched == 0,
			"tile %d on page %d: %d texels differ from its own page" % [p, page, mismatched])
		# Sanity that this case can actually fail: reading page 0 instead must
		# disagree somewhere, or the test proves nothing about rebinding.
		check(wrong_page > 0,
			"tile %d is identical on page 0, so this case cannot detect a page-0 binding" % p)
		# ...and now the preview itself, which is where the page-0 bug lived.
		# Asserting on FauxAssetSet alone passes even with rebinding removed:
		# it inspects the data behind the preview, not the preview.
		var preview := FauxAtlasPreview.new()
		preview.asset = multi
		add_child(preview)
		preview.select_picnum(p)
		check(preview.get_bound_page() == page,
			"preview bound page %d for a tile on page %d" % [preview.get_bound_page(), page])
		var wrong := 0
		for py in range(size.y):
			for px in range(size.x):
				var got := preview.bound_texel_at(rect.position.x + px, rect.position.y + py)
				if got != per_tile[py * size.x + px]:
					wrong += 1
		check(wrong == 0,
			"preview shows %d wrong texels for picnum %d on page %d" % [wrong, p, page])
		preview.queue_free()

		checked_above_zero = true
		break
	check(checked_above_zero, "no populated tile landed above page 0")

# The Image representation Godot would upload: R8, byte-identical.
func _assert_images(a: FauxAssetSet) -> void:
	var img = a.make_index_image(0)
	check(img != null, "index image should exist")
	if img == null:
		return
	check(img.get_format() == Image.FORMAT_R8,
		"index image format should be R8 (got %d)" % img.get_format())
	check(img.get_width() == PAGE_W and img.get_height() == PAGE_H, "index image dims")
	var data: PackedByteArray = img.get_data()
	var pixels := a.get_pixels()
	check(data.size() == pixels.size(), "image data size should equal atlas bytes")
	var mismatches := 0
	for j in range(data.size()):
		if data[j] != pixels[j]:
			mismatches += 1
			if mismatches <= 3:
				push_error("index image byte %d: got %d want %d" % [j, data[j], pixels[j]])
	check(mismatches == 0, "index image must be byte-identical to the atlas payload")

	check(a.get_shade_row_count() == 8, "fixture declares 8 shade rows")
	var shade_img = a.make_shade_image()
	check(shade_img != null and shade_img.get_height() == 8, "shade image should be 8x256")
	check(a.get_palette_choice_count() == 2, "palette choices: base + 1 alt")
	check(a.get_remap_choice_count() == 3, "remap choices: identity + 2 swaps")

# The palette-derived preview math the shader performs (data-level, so it
# is CI-verifiable headless; the visible preview is the human gate).
func _assert_preview_math(a: FauxAssetSet) -> void:
	# Base palette, no remap, shade row 0 (identity in the fixture):
	# color = palette[index_a(0,0)=17].
	var rgba: PackedByteArray = a.compute_tile_rgba(1, 0, 0, 0)
	check(rgba.size() == 8 * 8 * 4, "preview rgba should be 8x8*4 bytes")
	var want_r := six_to_eight(pal_component(3 * 17 + 0))
	var want_g := six_to_eight(pal_component(3 * 17 + 1))
	var want_b := six_to_eight(pal_component(3 * 17 + 2))
	check(rgba[0] == want_r and rgba[1] == want_g and rgba[2] == want_b and rgba[3] == 255,
		"base palette color for index 17: got %d,%d,%d want %d,%d,%d"
		% [rgba[0], rgba[1], rgba[2], want_r, want_g, want_b])

	# Shade row 3: fixture table is (c + 29*r) & 0xFF -> index 17 -> 104.
	var shaded: PackedByteArray = a.compute_tile_rgba(1, 3, 0, 0)
	var shown := (17 + 29 * 3) & 0xFF
	check(shaded[0] == six_to_eight(pal_component(3 * shown + 0)),
		"shade row 3 should remap 17 -> %d" % shown)

	# Alt palette 1: fixture is (i*5) & 0x3F.
	var alted: PackedByteArray = a.compute_tile_rgba(1, 0, 1, 0)
	check(alted[0] == six_to_eight(alt_component(3 * 17 + 0)), "alt palette 1 color")

	# Swap 1: fixture table is (c*3) & 0xFF -> 17 -> 51.
	var swapped: PackedByteArray = a.compute_tile_rgba(1, 0, 0, 1)
	var remapped := (17 * 3) & 0xFF
	check(swapped[0] == six_to_eight(pal_component(3 * remapped + 0)),
		"swap 1 should remap 17 -> %d" % remapped)
