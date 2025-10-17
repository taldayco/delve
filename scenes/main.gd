extends Node

@onready var map_generator = $MapGenerator

func _ready():
	# Wait one frame for MapGenerator to be ready
	await get_tree().process_frame
	test_map_generation()

func test_map_generation():
	print("\n=== Testing Map Generation ===")
	
	var map_data = map_generator.get_map_data()
	
	# Basic validation
	print("Map dimensions: ", map_data.width, "x", map_data.height)
	print("Total nodes: ", map_data.types.size())
	
	# Count node types
	var type_counts = {
		0: 0,  # NotAssigned
		1: 0,  # Enemy
		2: 0,  # Loot
		3: 0,  # Shelter
		4: 0,  # Wenny
		5: 0   # Boss
	}
	
	for i in map_data.types.size():
		var type = map_data.types[i]
		type_counts[type] += 1
	
	print("\nNode type distribution:")
	print("  NotAssigned: ", type_counts[0])
	print("  Enemy: ", type_counts[1])
	print("  Loot: ", type_counts[2])
	print("  Shelter: ", type_counts[3])
	print("  Wenny: ", type_counts[4])
	print("  Boss: ", type_counts[5])
	
	# Count connections
	var total_connections = map_data.connections.size() / 2
	print("\nTotal connections: ", total_connections)
	
	# Verify boss node
	var boss_row = map_data.height - 1
	var boss_col = int(map_data.width / 2)
	var boss_idx = boss_row * map_data.width + boss_col
	print("Boss node index: ", boss_idx, " Type: ", map_data.types[boss_idx], " (should be 5)")
	
	# Verify first row is all Enemy or NotAssigned
	print("\nFirst row types:")
	var first_row_str = "  "
	for col in map_data.width:
		var idx = 0 * map_data.width + col
		first_row_str += str(map_data.types[idx]) + " "
	print(first_row_str)
	
	print("\n=== Test Complete ===\n")

func _on_new_game_pressed():
	print("\n=== Regenerating Map ===")
	map_generator.regenerate_map()
	test_map_generation()

func _enter_tree():
	var button = $CanvasLayer/NewGameButton
	if button and not button.pressed.is_connected(_on_new_game_pressed):
		button.pressed.connect(_on_new_game_pressed)
