\

# delve_ml — Scalable Multi‑Biome Map Generation (ONNX Runtime)

This repository contains the high‑level architecture blueprints for a **lightweight, scalable ML subsystem** that generates stylized, subterranean maps for your roguelike. It targets **Godot + C++ (godot-cpp)** and uses **ONNX Runtime** for inference. The subsystem is a separate module/subproject intended for integration with _delve_.

---

## Project Layout (proposed)

```
delve-ml/
├─ include/
│  ├─ delve_ml/
│  │  ├─ ml_core.h
│  │  ├─ biome_generator.h
│  │  ├─ brutalist_biome.h
│  │  └─ world_generator.h
├─ src/
│  ├─ ml_core.cpp
│  ├─ biome_generator.cpp
│  ├─ brutalist_biome.cpp
│  └─ world_generator.cpp
├─ models/
│  ├─ core.onnx
│  ├─ adapters/
│  │  └─ brutalist_adapter.bin   (or brutalist_adapter_data.h if embedded)
├─ docs/
│  ├─ architecture/
│  │  ├─ ml_core.md
│  │  ├─ biome_generator.md
│  │  ├─ brutalist_biome.md
│  │  └─ world_generator.md
│  └─ README.md  (this file)
```

> **Build**: start with `make` during development; migrate to **CMake** later for cross‑platform deployment.  
> **Namespace**: `delve_ml` — **Class prefix**: `DelveMLCore`, `DelveBiomeGenerator`, etc.  
> **Inference backend**: **ONNX Runtime** (C++ API).  
> **Map output**: **Godot Dictionary** with named channels/layers.

---

## Ordered Implementation Roadmap

1. **Interfaces & Data Contracts (headers only)**
   - `include/delve_ml/biome_generator.h`
   - `include/delve_ml/ml_core.h`
   - `include/delve_ml/world_generator.h`
   - `include/delve_ml/brutalist_biome.h`
   - Define the **Dictionary** I/O schema and tensor shapes.

2. **ONNX Runtime Bootstrapping**
   - Implement minimal `DelveMLCore` that loads `models/core.onnx`, initializes a single session, and exposes:
     - `load_adapter(const BiomeAdapter&)`
     - `infer(...)` → tensor → `Dictionary`
   - Stub adapter application (no-op) to validate plumbing.

3. **Biome Base Class & Brutalist Implementation (skeleton)**
   - Implement `DelveBiomeGenerator` abstract base and `DelveBrutalistBiome` concrete class.
   - Hook into `DelveMLCore` for inference.
   - Implement deterministic _postprocessing_ (connectivity, flood fill).

4. **World Generator Node**
   - Implement `DelveWorldGenerator` (Godot-facing controller).
   - Expose methods to GDScript:
     - `set_biome(StringName biome)`
     - `generate_layout(int seed, int width, int height)`
     - `get_last_map()` returns `Dictionary`

5. **Adapter Format Choice & Loader**
   - Pick **binary adapter** (`.bin`) or **embedded header**.
   - Implement loader + validator; version your adapter format.

6. **End-to-End Smoke Test (No Godot)**
   - CLI test: seed → prior stub → `DelveMLCore::infer` → verify `Dictionary` schema and dimensions.

7. **Godot Integration Test**
   - Bind as GDExtension; generate layout from GDScript; render into TileMap; verify performance.

8. **Latent Prior (Per-Biome)**
   - Add minimal PixelCNN/Transformer **prior** as a _separate small ONNX_ for biome sampling (optional in v1).

9. **Metrics & Validation**
   - Connectivity %; room size histograms; artifact density; elevation continuity.

10. **CMake Migration**
    - Introduce CMakeLists.txt; link ONNX Runtime; set install rules and export headers.

---

## Adapter Storage: Binary vs Embedded (Summary)

- **Binary (`.bin`)**
  - ✅ Hot‑swappable at runtime; smaller repo footprint; no recompile to update adapters.
  - ✅ Versionable, can be downloaded/streamed; good for DLC‑like biome packs.
  - ⚠️ Requires a robust loader and checksum/version handling.
  - ⚠️ Slightly more runtime I/O.

- **Embedded Header (`*_adapter_data.h`)**
  - ✅ Zero runtime I/O; trivially simple to ship; nice for early prototyping.
  - ✅ Immutable once compiled; avoids file path issues.
  - ⚠️ Requires recompilation to update adapters.
  - ⚠️ Bloats binary and diffs; poor for modding/hot‑reload.

**Recommendation:** start with **binary** for long‑term scalability and hot‑swap; fall back to **embedded** only for bring‑up or CI determinism.

---

## Dictionary Output Schema (Godot)

The ML output is converted into a `Dictionary` with typed channels. Keys are `StringName` for speed.

```
{
  "width": int,
  "height": int,
  "seed": int,
  "channels": {
    "solid": PackedInt32Array,          // 0/1 mask (row-major, H*W)
    "artifact_density": PackedFloat32Array, // 0..1
    "elevation": PackedFloat32Array,        // normalized tiers 0..1
    "biome_id": int                     // enum/int for active biome
  },
  "metadata": {
    "version": int,
    "adapter_hash": String,
    "core_hash": String
  }
}
```

---

## Where to Start

Open `docs/architecture/*.md` and follow each component’s blueprint. Implement headers first, then the minimal ONNX loader, then wire the biome and world generator.
