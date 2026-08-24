extends Node

# Human inspection harness for gate A2. Deliberately NOT the CI consumer-boundary
# test: that script (atlas_preview_test.gd) asserts synthetic-fixture constants
# unconditionally — 11 picnums, one page, specific 8x8 tiles, synthetic palette
# formulas — and pointing it at a real archive produces a wall of expected
# mismatches that say nothing about the atlas.
#
# This script performs only generic, content-independent sanity checks and then
# stays open for interactive inspection. It asserts nothing about *which* tiles
# exist, because for a real archive we have no such expectation.
#
# Keeping the two separate is deliberate: making the load-bearing CI test
# conditional would make it easy to bypass by accident.

var _assets: FauxAssetSet
var _preview: FauxAtlasPreview
var _problems := 0

func _fail(message: String) -> void:
	_problems += 1
	push_error("atlas-preview: " + message)

func _ready() -> void:
	var source := ""
	var is_grp := false
	var args := OS.get_cmdline_user_args()
	var i := 0
	while i < args.size():
		if args[i] == "--grp" and i + 1 < args.size():
			source = args[i + 1]
			is_grp = true
			i += 2
		elif args[i] == "--dir" and i + 1 < args.size():
			source = args[i + 1]
			is_grp = false
			i += 2
		else:
			i += 1

	if source.is_empty():
		# No argument: fall back to the synthetic fixture so the harness itself
		# is exercised in CI and cannot rot between human runs.
		source = ProjectSettings.globalize_path("res://").path_join("../fixtures/atlas")
		is_grp = false
		print("atlas-preview: no --grp/--dir given, using the synthetic fixture")

	_assets = FauxAssetSet.new()
	var ok := _assets.load_grp(source) if is_grp else _assets.load_dir(source)
	if not ok:
		push_error("atlas-preview: load failed: " + _assets.get_last_error())
		get_tree().quit(1)
		return

	_check_generic(is_grp, source)

	_preview = FauxAtlasPreview.new()
	_preview.asset = _assets
	add_child(_preview)
	if not _preview.is_ready():
		_fail("FauxAtlasPreview failed to build")

	if _problems == 0:
		print("atlas-preview: ready for inspection")
	else:
		push_error("atlas-preview: %d generic check(s) failed" % _problems)
		get_tree().quit(1)

# Content-independent invariants only. Anything here must hold for any valid
# asset set, synthetic or real — no tile counts, no expected pixel values.
func _check_generic(is_grp: bool, source: String) -> void:
	var tiles := _assets.get_tile_count()
	var pages := _assets.get_page_count()
	var page_size := _assets.get_page_size()
	var pixels: PackedByteArray = _assets.get_pixels()
	var stats: Dictionary = _assets.get_stats()

	if tiles <= 0:
		_fail("tile count should be positive, got %d" % tiles)
	if pages <= 0:
		_fail("page count should be positive, got %d" % pages)
	if page_size.x <= 0 or page_size.y <= 0:
		_fail("page size should be positive, got %s" % str(page_size))

	# The indexed-authority tripwire: one byte per texel, never four.
	var expected := page_size.x * page_size.y * pages
	if pixels.size() != expected:
		_fail("indexed byte count %d != %d (RGBA leak?)" % [pixels.size(), expected])

	var populated: int = stats["populated_tiles"]
	if populated <= 0 or populated > tiles:
		_fail("populated tiles %d outside 1..%d" % [populated, tiles])

	# Every populated tile must sit inside its own page; every unpopulated one
	# must claim no page. True of any atlas, whatever the content.
	var checked := 0
	var per_page := {}
	for p in range(tiles):
		var meta: Dictionary = _assets.get_tile_meta(p)
		var size: Vector2i = _assets.get_tile_size(p)
		if not bool(meta["populated"]):
			if meta["page"] != -1:
				_fail("picnum %d is unpopulated but claims page %d" % [p, meta["page"]])
			continue
		var page: int = meta["page"]
		var rect: Rect2i = _assets.get_tile_rect(p)
		if page < 0 or page >= pages:
			_fail("picnum %d claims page %d of %d" % [p, page, pages])
		elif rect.position.x < 0 or rect.position.y < 0 \
				or rect.position.x + size.x > page_size.x \
				or rect.position.y + size.y > page_size.y:
			_fail("picnum %d rect %s escapes its page" % [p, str(rect)])
		per_page[page] = int(per_page.get(page, 0)) + 1
		checked += 1
	if checked != populated:
		_fail("walked %d populated tiles but stats say %d" % [checked, populated])

	var origin := "grp" if is_grp else "dir"
	print("atlas-preview: %s:%s" % [origin, source])
	print("atlas-preview: %d picnums, %d populated, %d page(s) of %dx%d, %d indexed bytes"
		% [tiles, populated, pages, page_size.x, page_size.y, pixels.size()])
	var spread := []
	for page in range(pages):
		spread.append("page %d: %d tiles" % [page, int(per_page.get(page, 0))])
	print("atlas-preview: " + ", ".join(spread))
	print("atlas-preview: palette choices %d, remap choices %d, shade rows %d"
		% [_assets.get_palette_choice_count(), _assets.get_remap_choice_count(),
		_assets.get_shade_row_count()])

	# A concrete starting point for the human: one populated picnum per page.
	var hints := []
	for page in range(pages):
		for p in range(tiles):
			var meta: Dictionary = _assets.get_tile_meta(p)
			if bool(meta["populated"]) and meta["page"] == page:
				var size: Vector2i = _assets.get_tile_size(p)
				hints.append("page %d: picnum %d (%dx%d)" % [page, p, size.x, size.y])
				break
	# ...and one of each empty kind, if present, to exercise the empty path.
	for p in range(tiles):
		var meta: Dictionary = _assets.get_tile_meta(p)
		if not bool(meta["populated"]) and bool(meta["claimed"]):
			hints.append("empty (zero-dimension): picnum %d" % p)
			break
	for p in range(tiles):
		var meta: Dictionary = _assets.get_tile_meta(p)
		if not bool(meta["populated"]) and not bool(meta["claimed"]):
			hints.append("empty (gap): picnum %d" % p)
			break
	print("atlas-preview: try -> " + "; ".join(hints))
