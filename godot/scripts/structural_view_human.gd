extends Node3D

# Human synthetic structural viewer (M5 slice 2). Deliberately separate from
# the fixture-constant CI scene (structural_view_test.gd): this harness makes
# the structural fixtures visually understandable and stays open for
# interactive inspection. Committed synthetic fixtures only -- it has no
# real-content loading capability at all (that is slice 3), and refuses to be
# pointed at any.
#
# Presentation: perspective camera with direct transform fly controls. No
# CharacterBody3D, no collision, no physics. Camera framing derives an AABB
# from the generated presentation meshes -- framing is presentation, never
# world authority.

const GROUPS := ["Floors", "Ceilings", "SolidWalls", "PortalUpper", "PortalLower"]
const SPEED := 10.0
const FAST_MULT := 4.0
const LOOK_SENSITIVITY := 0.0022

var view: FauxBuildView
var harness: FauxStructuralFixture
var camera: Camera3D
var fixture_name := "square_room"


func _ready() -> void:
	var args := OS.get_cmdline_user_args()
	var i := 0
	while i < args.size():
		if args[i] == "--fixture" and i + 1 < args.size():
			fixture_name = args[i + 1]
			i += 2
		elif args[i] == "--grp" or args[i] == "--map" or args[i] == "--dir":
			push_error("structural_view_human.gd presents committed synthetic fixtures "
				+ "only; proprietary content is out of scope for the M5 slice-2 viewer.")
			get_tree().quit(2)
			return
		else:
			i += 1

	harness = FauxStructuralFixture.new()
	if not harness.get_fixture_names().has(fixture_name):
		push_error("structural-view: unknown fixture '%s' (have: %s)"
			% [fixture_name, ", ".join(harness.get_fixture_names())])
		get_tree().quit(1)
		return

	view = FauxBuildView.new()
	add_child(view)
	camera = Camera3D.new()
	camera.fov = 70.0
	add_child(camera)
	camera.current = true

	if not harness.present(fixture_name, view):
		push_error("structural-view: fixture failed to present: " + harness.get_last_error())
		get_tree().quit(1)
		return

	_frame_camera()

	if DisplayServer.get_name() != "headless":
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	print("structural-view: ready for inspection (%s; %d notes, %d diagnostics)"
		% [fixture_name, harness.get_note_count(), harness.get_diagnostic_count()])
	print("structural-view: WASD move, Q/E down/up, Shift fast, mouse look, "
		+ "Escape releases the mouse, click recaptures")


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
