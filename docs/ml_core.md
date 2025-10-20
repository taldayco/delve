\
# DelveMLCore — ONNX Runtime Inference Core

**Namespace:** `delve_ml`  
**Class:** `DelveMLCore` (singleton `RefCounted`)  
**Role:** Owns ONNX Runtime environment/session(s), loads the shared core model once, swaps in biome adapters, runs inference, returns Godot `Dictionary` maps.

---

## Responsibilities

- Initialize **ONNX Runtime** (`Ort::Env`, `Ort::SessionOptions`, `Ort::Session`).
- Load `core.onnx` and keep it resident.
- Maintain **adapter state** (biome embedding, optional LoRA-like deltas).
- Provide `infer(...)` API that converts inputs → tensors → outputs.
- Versioning & telemetry: report `core_hash`, `adapter_hash` in metadata.

---

## Public API (C++)

```cpp
namespace delve_ml {

struct BiomeAdapter {
    // Minimal v1: only an embedding vector.
    std::vector<float> embedding;   // shape: [E], e.g., E=32
    std::string version;            // "adapter_v1"
    std::string hash_hex;           // for metadata
};

class DelveMLCore : public godot::RefCounted {
    GDCLASS(DelveMLCore, godot::RefCounted);

public:
    static godot::Ref<DelveMLCore> get_singleton();  // thread-safe lazy init

    // Load shared ONNX model (once).
    void load_core_model(const godot::String &core_onnx_path);

    // Install per-biome adapter (embedding / deltas).
    void load_adapter(const BiomeAdapter &adapter);

    // Optional: set latent prior outputs (already sampled latent grid).
    void set_latent(const godot::PackedFloat32Array &latent, int lw, int lh);

    // Run model to produce a map; returns a Dictionary with channels.
    godot::Dictionary infer(int seed, int width, int height, int biome_id);

    // Hash, version, diagnostics.
    godot::String get_core_hash() const;
    godot::String get_adapter_hash() const;

protected:
    static void _bind_methods();

private:
    // Internal ONNX objects (opaque handles).
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace delve_ml
```

---

## I/O Tensor Contracts

### Inputs
- `seed` (int) is used outside the model for latent sampling or augmentations.
- `input_latent` (optional): `[1, Lh, Lw, Ld]` float32 — latent grid (when provided).
- `biome_embedding`: `[1, E]` float32 — per-biome vector.
- `spatial_hint` (optional): `[1, H, W, 1]` float32 — mask (e.g., connectivity constraints).

> For v1, you can omit `input_latent` and `spatial_hint` and let the core generate from noise inside the network (simpler path).

### Outputs
- `solid`: `[1, H, W, 1]` float32 — threshold to 0/1.
- `artifact_density`: `[1, H, W, 1]` float32 (0..1).
- `elevation`: `[1, H, W, 1]` float32 (0..1).

The core converts these to a `Dictionary` (see schema in README).

---

## Adapter Application (v1)

- **Embedding‑only**: concatenate or FiLM‑modulate internal activations.
- Future: **LoRA‑style** conv deltas:
  - Store `A` and `B` low‑rank matrices per targeted conv; apply as `W' = W + A @ B`.

Keep a small **adapter registry** in `_impl` with active adapter pointers.

---

## Error Handling & Preconditions

- Throw/ERR_FAIL if `core.onnx` not loaded.
- Validate `embedding` size == E.
- Verify ONNX input/output names at load time.
- Return a `Dictionary` with `"error"` key if inference fails (never crash Godot).

---

## Threading

- ONNX `Session` is thread‑safe for inference; create one session, run on a worker thread if needed.
- Guard adapter swaps with a mutex.  
- Avoid allocations in hot loops (reuse Ort::Value buffers).

---

## Minimal Make Integration

- Link against onnxruntime (`-lonnxruntime`); ensure rpaths or deploy shared lib beside the game binary.
- Provide a compile‑time toggle `#define DELVE_ML_EMBED_ADAPTERS 0/1` to switch adapter source.

---

## Telemetry

- `core_hash`: SHA256 or file mtime+size hash of `core.onnx`.
- `adapter_hash`: from `BiomeAdapter::hash_hex`.
- Expose both via `metadata` in output.
