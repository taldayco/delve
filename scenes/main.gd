extends Node

@onready var map_generator = $MapGenerator
@onready var new_game_button = $CanvasLayer/NewGameButton

func _ready():
	new_game_button.pressed.connect(_on_new_game_pressed)

func _on_new_game_pressed():
	map_generator.regenerate_map()
