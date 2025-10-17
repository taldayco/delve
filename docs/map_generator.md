# Map Generation Module

Procedural roguelike map generator for **Delve** - generates connected node-based maps with weighted room types.

## Features

- **Procedural generation** - Random map layouts with configurable paths
- **Node-based structure** - Grid of connected rooms forming branching paths
- **Weighted room types** - Enemy encounters, shelters, merchant (Wenny), and boss rooms
- **Anti-crossing logic** - Paths never cross each other
- **Boss convergence** - All paths converge at final boss node

## Configuration

Defined in `MapConfig` namespace:

```cpp
HEIGHT = 15              // Grid rows
WIDTH = 7                // Grid columns  
PATHS = 6                // Number of starting paths
X_DIST = 30              // Horizontal spacing
Y_DIST = 25              // Vertical spacing
PLACEMENT_RANDOMNESS = 30 // Position variance
```

Room type weights in `NodeWeights`:

```cpp
ENEMY = 10.0f    // Combat encounters
WENNY = 2.5f     // Merchant/shop
SHELTER = 4.0f   // Rest/heal points
LOOT = 0.0f      // Treasure (unused)
```

## Usage

### GDScript

```gdscript
@onready var map_generator = $MapGenerator

func generate_new_map():
    map_generator.regenerate_map()
    var data = map_generator.get_map_data()
    
    # Access map properties
    print("Size: ", data.width, "x", data.height)
    print("Node at (5,3): Type=", data.types[5 * data.width + 3])
```

### API

**`regenerate_map()`**  
Generates a new random map with randomized seed.

**`get_map_data() -> Dictionary`**  
Returns packed map data:

```gdscript
{
    "width": int,               # Grid width
    "height": int,              # Grid height
    "types": PackedInt32Array,  # Node types (0-5)
    "rows": PackedInt32Array,   # Row indices
    "columns": PackedInt32Array, # Column indices
    "positions": PackedVector2Array, # World positions
    "connection_counts": PackedByteArray, # Connections per node
    "connections": PackedInt32Array # [src, dst, src, dst, ...]
}
```

## Node Types

| Value | Type | Description |
|-------|------|-------------|
| 0 | NotAssigned | Unconnected/inactive node |
| 1 | Enemy | Combat encounter |
| 2 | Loot | Treasure (unused) |
| 3 | Shelter | Rest point |
| 4 | Wenny | Merchant |
| 5 | Boss | Final encounter |

## Generation Rules

- **Row 0**: Always Enemy nodes
- **Row 8**: Always Shelter nodes  
- **Row HEIGHT-2**: Always Shelter nodes (pre-boss)
- **Row HEIGHT-1, Col WIDTH/2**: Boss node
- **Other rows**: Weighted random with constraints:
  - No consecutive Wenny nodes
  - No consecutive Shelter nodes
  - Shelters only available from row 3+

## Example Output

Typical generation stats:
- Total nodes: 105 (7×15)
- Connected nodes: 50-61
- Enemy nodes: 26-37
- Shelter nodes: 13-21
- Wenny nodes: 3-12
- Boss nodes: 1
- Total connections: 80-90

## Architecture

- **Plain data structs** - No Godot Resource overhead
- **Flat memory layout** - Single vector storage
- **Zero-initialization** - All data defaults to valid state
- **Cache-friendly** - Contiguous memory access

## Files

```
include/map/map_generator.h    # Interface & config
src/map/map_generator.cpp      # Implementation
src/register_types.cpp         # Godot registration
```

## Performance

- Generation: <1ms for 15×7 grid
- Memory: ~12KB per map (105 nodes × ~115 bytes)
- Zero heap fragmentation
- No allocations during regeneration
