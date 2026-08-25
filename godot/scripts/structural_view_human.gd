extends Node3D

# Human structural viewer (M5 slices 2-3). Deliberately separate from the
# fixture-constant CI scene (structural_view_test.gd) and from the
# production-route CI scene (structural_source_test.gd): this harness is
# what a human inspects, and (since slice 3) it is the ONLY place real
# content may enter.
#
# Modes:
#   --fixture NAME              committed synthetic fixture (default)
#   --dir PATH --map VFS_NAME   loose directory source
#   --grp PATH --map VFS_NAME   GRP archive source (mounted in place, no
#                               extraction) through the production
#                               FauxStructuralSource route
#
# The real modes print the generic source facts (sectors, walls,
# structural surfaces, triangles, notes, diagnostics, groups) before the
# shell becomes inspectable. No expected content values are hardcoded:
# divergence is judged by the human against independently established
# numbers.
#
# Presentation: perspective camera with direct transform fly controls. No
# CharacterBody3D, no collision, no physics. Camera framing derives an AABB
# from the generated presentation meshes -- framing is presentation, never
# world authority.
#
# Everything below the geometry is HUMAN-VIEWER PRESENTATION ONLY and is
# deliberately non-contractual: the readability palette (applied as a
# material_override, so FauxBuildView's own diagnostic materials are
# untouched), the dark background, per-group visibility toggles, and the
# framing/traversal maths. None of it changes mesh contents, and no test
# asserts a colour. Ceilings start hidden so the shell can be inspected
# like an open model; C restores them.

const GROUPS := ["Floors", "Ceilings", "SolidWalls", "PortalUpper", "PortalLower"]
const KINDS := [0, 1, 2, 3, 4]
const SPEED := 10.0
const FAST_MULT := 4.0
const LOOK_SENSITIVITY := 0.0022

# Readability palette (presentation only, non-contractual). Neutral greys
# for structure, restrained warm tones for the portal bands so openings
# read at a glance without competing with the architecture.
const PALETTE := {
	"Floors": Color(0.62, 0.62, 0.63),
	"Ceilings": Color(0.30, 0.30, 0.32),
	"SolidWalls": Color(0.88, 0.87, 0.85),
	"PortalUpper": Color(0.80, 0.58, 0.24),
	"PortalLower": Color(0.66, 0.31, 0.24),
}
const BACKGROUND := Color(0.07, 0.07, 0.08)
const HIDDEN_BY_DEFAULT := ["Ceilings"]
const TOGGLE_KEYS := {
	KEY_1: "Floors",
	KEY_2: "Ceilings",
	KEY_3: "SolidWalls",
	KEY_4: "PortalUpper",
	KEY_5: "PortalLower",
}
const USAGE := "usage: [--fixture NAME] | [--grp PATH | --dir PATH] --map VFS_NAME"

var view: FauxBuildView
var camera: Camera3D
var fixture_name := "square_room"
var fixture_given := false
var grp_path := ""
var dir_path := ""
var map_name := ""
var move_speed := SPEED


func _usage_error(message: String) -> void:
	push_error("structural-view: " + message + " (" + USAGE + ")")
	get_tree().quit(2)


func _parse_args() -> bool:
	var args := OS.get_cmdline_user_args()
	var i := 0
	while i < args.size():
		var a := args[i]
		if a != "--fixture" and a != "--grp" and a != "--dir" and a != "--map":
			_usage_error("unknown argument '%s'" % a)
			return false
		if i + 1 >= args.size() or args[i + 1].begins_with("--"):
			_usage_error("%s needs a value" % a)
			return false
		var value: String = args[i + 1]
		match a:
			"--fixture":
				fixture_name = value
				fixture_given = true
			"--grp":
				grp_path = value
			"--dir":
				dir_path = value
			"--map":
				map_name = value
		i += 2

	if grp_path != "" and dir_path != "":
		_usage_error("--grp and --dir are alternative source modes; give only one")
		return false
	if fixture_given and (grp_path != "" or dir_path != ""):
		_usage_error("--fixture is the synthetic mode; do not combine it with --grp/--dir")
		return false
	if map_name != "" and grp_path == "" and dir_path == "":
		_usage_error("--map requires --grp or --dir")
		return false
	if (grp_path != "" or dir_path != "") and map_name == "":
		_usage_error("source mode requires --map (the MAP's VFS name inside the source)")
		return false
	return true


func _ready() -> void:
	if not _parse_args():
		return

	view = FauxBuildView.new()
	add_child(view)
	camera = Camera3D.new()
	camera.fov = 70.0
	add_child(camera)
	camera.current = true

	if grp_path != "" or dir_path != "":
		if not _present_source():
			return
	else:
		if not _present_fixture():
			return

	_apply_presentation()
	_frame_camera()

	# Permanent metric diagnostic (D0016 amendment): the world's render-space
	# extent, printed so vertical scale is measurable rather than judged by
	# eye. A map that reads as a tower shows it here as a Y far larger than
	# its horizontal extents.
	var world_bounds := _bounds()
	print("bounds size X/Y/Z: %.4f / %.4f / %.4f"
		% [world_bounds.size.x, world_bounds.size.y, world_bounds.size.z])

	if DisplayServer.get_name() == "headless":
		_headless_probe()
	if DisplayServer.get_name() != "headless":
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	print("structural-view: WASD move, Q/E down/up, Shift fast, mouse look, "
		+ "Escape releases the mouse, click recaptures")
	print("structural-view: C toggles ceilings (hidden at start); 1-5 toggle "
		+ "floors/ceilings/solid walls/portal upper/portal lower")


func _present_source() -> bool:
	var source := FauxStructuralSource.new()
	var ok := false
	if grp_path != "":
		ok = source.present_grp(grp_path, map_name, view)
	else:
		ok = source.present_dir(dir_path, map_name, view)
	if not ok:
		push_error("structural-view: source failed to load/present: " + source.get_last_error())
		get_tree().quit(1)
		return false

	print("structural-view source: %s" % source.get_source_description())
	print("map: %s" % source.get_map_name())
	print("sectors: %d" % source.get_sector_count())
	print("walls: %d" % source.get_wall_count())
	print("structural surfaces: %d" % source.get_surface_count())
	print("triangles: %d" % source.get_triangle_count())
	print("notes: %d" % source.get_note_count())
	print("diagnostics: %d" % source.get_diagnostic_count())
	print("groups: %s" % ", ".join(view.get_group_names()))
	print("structural-view: ready for inspection (%s:%s; %d notes, %d diagnostics)"
		% [source.get_source_description(), source.get_map_name(),
			source.get_note_count(), source.get_diagnostic_count()])
	return true


func _present_fixture() -> bool:
	var harness := FauxStructuralFixture.new()
	if not harness.get_fixture_names().has(fixture_name):
		push_error("structural-view: unknown fixture '%s' (have: %s)"
			% [fixture_name, ", ".join(harness.get_fixture_names())])
		get_tree().quit(1)
		return false
	if not harness.present(fixture_name, view):
		push_error("structural-view: fixture failed to present: " + harness.get_last_error())
		get_tree().quit(1)
		return false

	var surfaces := 0
	var triangles := 0
	for k in KINDS:
		surfaces += harness.expected_surface_count(k)
		triangles += harness.expected_indices(k).size() / 3
	print("structural-view source: fixture:%s" % fixture_name)
	print("map: -")
	print("structural surfaces: %d" % surfaces)
	print("triangles: %d" % triangles)
	print("notes: %d" % harness.get_note_count())
	print("diagnostics: %d" % harness.get_diagnostic_count())
	print("groups: %s" % ", ".join(view.get_group_names()))
	print("structural-view: ready for inspection (%s; %d notes, %d diagnostics)"
		% [fixture_name, harness.get_note_count(), harness.get_diagnostic_count()])
	return true


func _bounds() -> AABB:
	# AABB over the presented meshes; presentation-only convenience. Groups
	# hidden for readability still contribute, so the framing does not jump
	# when a toggle is pressed.
	var bounds := AABB()
	var have := false
	for group in GROUPS:
		var inst := view.get_node_or_null(group)
		if inst == null or inst.mesh == null:
			continue
		var box: AABB = inst.global_transform * inst.mesh.get_aabb()
		bounds = box if not have else bounds.merge(box)
		have = true
	return bounds if have else AABB()


func _apply_presentation() -> void:
	# Readability overrides on the generated group nodes. material_override
	# leaves FauxBuildView's own diagnostic materials and every mesh array
	# untouched; visibility is a node flag, not mesh content.
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = BACKGROUND
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(1, 1, 1)
	var world_environment := WorldEnvironment.new()
	world_environment.environment = environment
	add_child(world_environment)

	for group in GROUPS:
		var inst := view.get_node_or_null(group)
		if inst == null:
			continue
		var material := StandardMaterial3D.new()
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.cull_mode = BaseMaterial3D.CULL_DISABLED
		material.albedo_color = PALETTE.get(group, Color(0.7, 0.7, 0.7))
		inst.material_override = material
		inst.visible = not HIDDEN_BY_DEFAULT.has(group)


func _frame_camera() -> void:
	# Framing from the HORIZONTAL (X/Z) footprint, because real maps are
	# broad and shallow: sizing the pull-back on the full 3D diagonal let a
	# tall-but-narrow map push the camera far away, and a fixed diagonal
	# offset could end up looking straight DOWN the long axis, which is the
	# least informative direction.
	#
	# So: pull back along the MINOR horizontal axis with a smaller component
	# along the dominant one. The dominant axis then spans the view instead
	# of receding into it, and the offset stays diagonal rather than
	# axis-aligned. Elevation is a moderate rise above the AABB centre --
	# high enough to read the plan with ceilings hidden, never parked at the
	# map's vertical extreme.
	#
	# Presentation only. No Build angle or start-pose semantics are consulted;
	# this reads the generated meshes' bounds and nothing else.
	var bounds := _bounds()
	if bounds.size == Vector3.ZERO:
		return
	var center := bounds.get_center()
	var extent_x := maxf(bounds.size.x, 0.001)
	var extent_z := maxf(bounds.size.z, 0.001)
	var horizontal_radius := maxf(0.5 * sqrt(extent_x * extent_x + extent_z * extent_z), 0.001)

	var distance := horizontal_radius / tan(deg_to_rad(camera.fov * 0.5)) * 1.15
	var offset := (Vector3(0.45, 0.0, 1.0) if extent_x >= extent_z
		else Vector3(1.0, 0.0, 0.45)).normalized()
	var elevation := maxf(horizontal_radius * 0.45, bounds.size.y * 0.75)

	var eye := center + offset * distance
	eye.y = center.y + elevation
	camera.global_position = eye
	camera.look_at(center, Vector3.UP)

	# Clip planes and traversal speed scale with the world: the stock 4000-unit
	# far plane and fixed 10 units/s are unusable at real-map scale.
	var span := maxf(distance + bounds.size.length(), 1.0)
	camera.near = clampf(span * 0.0004, 0.05, 4.0)
	camera.far = span * 4.0
	move_speed = maxf(SPEED, horizontal_radius * 0.35)


func _toggle_group(group: String) -> void:
	var inst := view.get_node_or_null(group)
	if inst != null:
		inst.visible = not inst.visible


func _headless_probe() -> void:
	# Headless-only diagnostic, never reached in a human session. It prints
	# raw observations and makes no judgement: ci/check_scene.py decides
	# whether the framing points at the bounds and whether toggling left the
	# arrays alone, so a broken viewer cannot declare itself correct.
	var bounds := _bounds()
	var center := bounds.get_center()
	var forward := -camera.global_transform.basis.z
	print("structural-view framing: eye=(%.6f,%.6f,%.6f) center=(%.6f,%.6f,%.6f) "
		% [camera.global_position.x, camera.global_position.y, camera.global_position.z,
			center.x, center.y, center.z]
		+ "fwd=(%.6f,%.6f,%.6f) size=(%.6f,%.6f,%.6f) near=%.6f far=%.6f speed=%.6f"
		% [forward.x, forward.y, forward.z, bounds.size.x, bounds.size.y, bounds.size.z,
			camera.near, camera.far, move_speed])

	var ceilings := view.get_node_or_null("Ceilings")
	if ceilings == null or ceilings.mesh == null:
		print("structural-view toggle: absent")
		return
	var before: Array = ceilings.mesh.surface_get_arrays(0)
	var mesh_before: int = ceilings.mesh.get_instance_id()
	var visible_before: bool = ceilings.visible
	_toggle_group("Ceilings")
	var visible_mid: bool = ceilings.visible
	_toggle_group("Ceilings")
	var after: Array = ceilings.mesh.surface_get_arrays(0)
	print("structural-view toggle: vis=%s,%s,%s mesh=%d,%d vhash=%d,%d ihash=%d,%d"
		% [visible_before, visible_mid, ceilings.visible,
			mesh_before, ceilings.mesh.get_instance_id(),
			hash(before[Mesh.ARRAY_VERTEX]), hash(after[Mesh.ARRAY_VERTEX]),
			hash(before[Mesh.ARRAY_INDEX]), hash(after[Mesh.ARRAY_INDEX])])


func _physics_process(delta: float) -> void:
	if camera == null:
		return
	var basis := camera.global_transform.basis
	var move := Vector3.ZERO
	if Input.is_key_pressed(KEY_W):
		move -= basis.z
	if Input.is_key_pressed(KEY_S):
		move += basis.z
	if Input.is_key_pressed(KEY_A):
		move -= basis.x
	if Input.is_key_pressed(KEY_D):
		move += basis.x
	if Input.is_key_pressed(KEY_Q):
		move -= basis.y
	if Input.is_key_pressed(KEY_E):
		move += basis.y
	if move != Vector3.ZERO:
		var mult := FAST_MULT if Input.is_key_pressed(KEY_SHIFT) else 1.0
		camera.global_position += move.normalized() * move_speed * mult * delta


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		camera.rotate_y(-event.relative.x * LOOK_SENSITIVITY)
		camera.rotate_object_local(Vector3(1, 0, 0), -event.relative.y * LOOK_SENSITIVITY)
	elif event is InputEventKey and event.pressed and event.keycode == KEY_ESCAPE:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	elif event is InputEventKey and event.pressed and event.keycode == KEY_C:
		_toggle_group("Ceilings")
	elif event is InputEventKey and event.pressed and TOGGLE_KEYS.has(event.keycode):
		_toggle_group(TOGGLE_KEYS[event.keycode])
	elif event is InputEventMouseButton and event.pressed \
			and Input.mouse_mode == Input.MOUSE_MODE_VISIBLE:
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
