\
# DelveBiomeGenerator — Abstract Biome Interface

**Namespace:** `delve_ml`  
**Class:** `DelveBiomeGenerator` (abstract)  
**Role:** Defines the contract for any biome module; owns biome‑specific adapter/prior; orchestrates ML inference + postprocessing.

---

## Responsibilities

- Hold **biome identity** (enum/int id, human name).
- Provide **adapter** to `DelveMLCore`.
- Optionally sample a **latent prior** (separate small ONNX or CPU sampler).
- Run **postprocessing** (connectivity, flood fill, artifact placement clamps).
- Expose generated map as a **Godot Dictionary**.

---

## Public API (C++)

```cpp
namespace delve_ml {

enum class BiomeId : int {
    Brutalist = 1,
    // Future: Fungal = 2, Crystal = 3, Flooded = 4, ...
};

class DelveBiomeGenerator {
public:
    virtual ~DelveBiomeGenerator() = default;

    virtual BiomeId get_biome_id() const = 0;
    virtual godot::String get_name() const = 0;

    // Main entry point: seed + size → Dictionary map.
    virtual godot::Dictionary generate_map(
        int seed, int width, int height, godot::Ref<DelveMLCore> core) = 0;
};

} // namespace delve_ml
```

---

## Postprocessing Rules (Recommended)

1. **Connectivity**
   - Flood fill largest reachable component from spawn; convert other floor regions to solid.
2. **Room Cleanup**
   - Remove single‑tile islands; close < 2‑tile corridors.
3. **Artifact Density**
   - Clamp to biome min/max; apply Poisson‑disk if converting to placements.
4. **Elevation Smoothing**
   - Quantize to tiers; ensure ramps between tiers are valid.

---

## Data Contract

- Must return a `Dictionary` following the project schema:
  - include `"biome_id"` and `metadata.adapter_hash`.
- Width/height must match request.
- All channels are row‑major arrays (H*W).

---

## Testing

- Pure C++ unit test: deterministic RNG → stable map stats.
- Metrics: connectivity ratio ≥ threshold; artifact density within range.
