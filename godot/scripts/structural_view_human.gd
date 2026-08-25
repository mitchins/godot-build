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

const GROUPS := ["Floors", "Ceilings", "SolidWalls", "PortalUpper", "PortalLower"]
const KINDS := [0, 1, 2, 3, 4]
const SPEED := 10.0
const FAST_MULT := 4.0
const LOOK_SENSITIVITY := 0.0022
const USAGE := "usage: [--fixture NAME] | [--grp PATH | --dir PATH] --map VFS_NAME"

var view: FauxBuildView
var camera: Camera3D
var fixture_name := "square_room"
var fixture_given := false
var grp_path := ""
var dir_path := ""
var map_name := ""


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

	_frame_camera()

	if DisplayServer.get_name() != "headless":
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	print("structural-view: WASD move, Q/E down/up, Shift fast, mouse look, "
		+ "Escape releases the mouse, click recaptures")


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


func _frame_camera() -> void:
	# AABB over the presented meshes; presentation-only convenience.
	var bounds := AABB()
	var have := false
	for group in GROUPS:
		var inst := view.get_node_or_null(group)
		if inst == null or inst.mesh == null:
			continue
		var box: AABB = inst.global_transform * inst.mesh.get_aabb()
		bounds = box if not have else bounds.merge(box)
		have = true
	if not have:
		return
	var center := bounds.get_center()
	var radius := maxf(bounds.size.length() * 0.5, 0.001)
	var distance := radius / tan(deg_to_rad(camera.fov * 0.5)) * 1.35
	var direction := Vector3(0.55, 0.75, 0.85).normalized()
	camera.global_position = center + direction * distance
	camera.look_at(center, Vector3.UP)


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
		camera.global_position += move.normalized() * SPEED * mult * delta


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		camera.rotate_y(-event.relative.x * LOOK_SENSITIVITY)
		camera.rotate_object_local(Vector3(1, 0, 0), -event.relative.y * LOOK_SENSITIVITY)
	elif event is InputEventKey and event.pressed and event.keycode == KEY_ESCAPE:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	elif event is InputEventMouseButton and event.pressed \
			and Input.mouse_mode == Input.MOUSE_MODE_VISIBLE:
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
