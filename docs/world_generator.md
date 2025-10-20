\
# DelveWorldGenerator — Godot-Facing Controller

**Namespace:** `delve_ml`  
**Class:** `DelveWorldGenerator` (Godot Node)  
**Role:** Central controller that selects the biome, triggers generation, and exposes the result to the engine/renderer.

---

## Responsibilities

- Hold active biome instance (pointer/ref).
- Manage `DelveMLCore` singleton reference.
- Provide Godot‑exposed methods to select biome, set size, generate layouts.
- Convert `Dictionary` → TileMap (outside of this module’s concern, but provide helper).

---

## Public API (Godot Bindings)

```cpp
namespace delve_ml {

class DelveWorldGenerator : public godot::Node {
    GDCLASS(DelveWorldGenerator, godot::Node);

public:
    void set_size(int width, int height);
    godot::Vector2i get_size() const;

    void set_biome_brutalist(); // v1 convenience
    void set_biome_by_id(int biome_id);

    godot::Dictionary generate_layout(int seed);

    // Last result for debugging or rendering.
    godot::Dictionary get_last_map() const;

protected:
    static void _bind_methods();

private:
    int _width = 64, _height = 64;
    godot::Ref<DelveMLCore> _core;
    std::unique_ptr<DelveBiomeGenerator> _biome;
    godot::Dictionary _last_map;
};

} // namespace delve_ml
```

---

## Data Flow

```
set_biome_*() → holds DelveBiomeGenerator
generate_layout(seed) → biome.generate_map(seed, W, H, core)
  → Dictionary map (channels, metadata)
get_last_map() → returns Dictionary
```

---

## Notes

- Keep this node thin; it should not contain game‑specific rules beyond map fetch/forward.
- Rendering/Tileset conversion should live in your game layer to preserve separation of concerns.
