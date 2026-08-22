extends Node


func _ready() -> void:
	var runtime := get_node("../FauxBuildRuntime")
	print("FauxBuild core version: ", runtime.get_core_version())
	print("FauxBuild M1 sample scene: OK")
