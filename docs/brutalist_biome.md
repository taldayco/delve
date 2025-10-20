\
# DelveBrutalistBiome — Concrete Biome (v1)

**Namespace:** `delve_ml`  
**Class:** `DelveBrutalistBiome` (implements `DelveBiomeGenerator`)  
**Goal:** Angular, geometric halls; repetition & symmetry; large basalt voids; dense clusters of arcane artifacts; vertical depth tiers; sparse/washed lighting implications.

---

## Biome Adapter

- **Embedding** size E = 32 (example).  
- Stored as **binary** `models/adapters/brutalist_adapter.bin` (recommended) or embedded header for prototyping.
- Version string `"adapter_v1"`; include `hash_hex` for telemetry.

---

## Generation Pipeline (Brutalist)

```
seed → biome RNG
   → (optional) latent prior sample [Lh×Lw×Ld]
   → DelveMLCore::load_adapter(brutalist_adapter)
   → DelveMLCore::infer(seed, W, H, biome_id=Brutalist)
   → postprocess (connectivity, symmetry clamp, artifact density policy)
   → Dictionary output
```

### Symmetry Clamp (optional)
- Mirror horizontally or vertically with probability p.
- Enforce axis‑aligned corridor bias by pruning diagonals.

### Artifact Policy
- Convert `artifact_density` (0..1) to placements:
  - Threshold, Poisson‑disk with radius r by tier.
  - Reserve artifacts in “void edges” (contact surfaces).

### Elevation Policy
- Quantize elevation to `{0.0, 0.33, 0.66, 1.0}` tiers.
- Ensure walkable transitions between adjacent tiers (stair/bridge tiles).

---

## Public C++ Sketch

```cpp
namespace delve_ml {

class DelveBrutalistBiome : public DelveBiomeGenerator {
public:
    explicit DelveBrutalistBiome(const BiomeAdapter &adapter);

    BiomeId get_biome_id() const override { return BiomeId::Brutalist; }
    godot::String get_name() const override { return "brutalist"; }

    godot::Dictionary generate_map(
        int seed, int width, int height, godot::Ref<DelveMLCore> core) override;

private:
    BiomeAdapter _adapter; // embedding + hash/version

    // Helpers
    void _apply_connectivity(godot::PackedInt32Array &solid, int w, int h) const;
    void _apply_artifacts(godot::PackedFloat32Array &artifact_density,
                          const godot::PackedInt32Array &solid,
                          int w, int h) const;
    void _quantize_elevation(godot::PackedFloat32Array &elev, int w, int h) const;
};

} // namespace delve_ml
```

---

## Metrics (Brutalist)

- Corridor straightness score ≥ threshold.
- Symmetry score (SSIM on mirrored halves) within range.
- Target void area fraction `~0.25–0.4`.
