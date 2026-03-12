Plan: Skeletal Animation System (Replace Procedural Rig)

Context

The current character renderer (RigRenderer) generates a procedural bone-cage
mesh on the CPU every frame (~65k vertices of wireframe struts/octahedra) and
uploads it using the terrain pipeline's BasaltVertex format. There is no GPU
skinning — joint positions are computed in ECS systems and the mesh is rebuilt
from scratch.

The goal is to replace this with a proper skeletal animation pipeline: load a
rigged mesh from Blender (86k vertex "Hollow Net" character), skin it on the
GPU using Linear Blend Skinning, and play imported animation clips (Idle,
Walk, Run, Turn 180).

Critical constraint: The isometric camera is already working — do not touch
it.

Assets (complete):
assets/characters/wireframe_character.glb (mesh + skeleton + rest pose)
assets/characters/anim_idle.glb
assets/characters/anim_walk.glb
assets/characters/anim_run.glb
assets/characters/anim_turn_180.glb

---

Phase 1: Extend glTF Loader

File: src/engine/core/gltf_loader.h — Add new types:

- SkinnedVertex (68B): position(vec3), normal(vec3), texcoord(vec2),
  tangent(vec4), joints(uint8_t[4]), weights(float[4])
- GltfBone: name, parent_index, inverse_bind_matrix, local_rest_transform
- GltfSkeleton: vector of GltfBone, root_bone_index
- GltfAnimationClip: name, duration, vector of channels (bone_index, path,
  keyframes with time/translation/rotation/scale)
- GltfSkinnedAsset: meshes (using SkinnedVertex), textures, skeleton,
  animations
- New function: GltfSkinnedAsset load_gltf_skinned(const std::string &path)

File: src/engine/core/gltf_loader.cpp — Implement load_gltf_skinned():

- Parse cgltf_attribute_type_joints and cgltf_attribute_type_weights in vertex
  extraction
- Parse cgltf_skin for inverse bind matrices and joint hierarchy
- Parse cgltf_animation channels — binary keyframes for
  translation/rotation/scale per bone
- Quaternion note: glTF stores [x,y,z,w] but glm::quat constructor takes
  (w,x,y,z)

---

Phase 2: Runtime Skeleton & Animation

New file: src/game/render/skeletal_animation.h/cpp

- BoneLocalTransform: translation(vec3), rotation(quat), scale(vec3)
- BonePalette: glm::mat4 bones[65] — the SSBO payload
- AnimationPlayer: holds clip pointer + time, update(dt) advances time,
  sample() evaluates keyframes into per-bone local transforms (lerp pos/scale,
  slerp rotation)
- compute_bone_palette(): walks hierarchy top-down computing
  GlobalTransform[i] = Parent _Local, then FinalMatrix[i] = Global[i]_
  InverseBind[i]

---

Phase 3: New Shaders

New file: src/shaders/skinned_character.vert.glsl

layout(location=0) in vec3 in_pos;
layout(location=1) in vec3 in_normal;
layout(location=2) in vec2 in_texcoord;
layout(location=3) in vec4 in_tangent;
layout(location=4) in uvec4 in_joints;
layout(location=5) in vec4 in_weights;

layout(set=0, binding=0) readonly buffer BoneBuffer { mat4 bones[65]; };

// LBS: skin_mat = sum(weight[i]_ bones[index[i]])
// gl_Position = projection_ view _skin_mat _vec4(in_pos, 1.0)

New file: src/shaders/skinned_character.frag.glsl

- Copy terrain.frag.glsl lighting model (Lambertian + point lights at set=2
  binding=0)
- Same SceneUniforms at set=1

File: CMakeLists.txt — Add both to GRAPHICS_SHADER_SOURCES

---

Phase 4: SkinnedRenderer Class

New file: src/game/render/skinned_renderer.h/cpp

Init

- Create pipeline: vertex shader (1 UBO, 1 SSBO), fragment shader (1 UBO, 3
  SSBOs)
- Vertex attributes: 6 attrs matching SkinnedVertex layout (FLOAT3, FLOAT3,
  FLOAT2, FLOAT4, UBYTE4, FLOAT4)
- Depth test LESS_OR_EQUAL, depth write on (same as terrain)
- Create bone_ssbo (GRAPHICS_STORAGE_READ, sizeof(BonePalette))
- Create bone_transfer_buf (persistent mapped staging buffer)

Load

- load_character(path): call load_gltf_skinned(), upload static VBO/IBO via
  gpu_upload_buffer()
- load_animation(name, path): load GLB, extract clip, store in map

Per-frame

- update(dt): advance AnimationPlayer, sample keyframes, compute bone palette
- prepare(cmd): map transfer buffer, memcpy palette, copy pass to bone_ssbo
- draw(pass, cmd, uniforms, lights_ssbo, dummy_ssbo, model_matrix):
  - Bind pipeline, push uniforms, bind bone_ssbo at vertex storage slot 0
  - Bind light SSBOs at fragment storage slots 0-2
  - Bind VBO/IBO, draw indexed

Coordinate handling

Root transform composed into bone palette (pre-multiplied into every bone
matrix):
root = translate(player_pos)_ rotate(facing, Z) _scale(1, 1,
ISO_HEIGHT_SCALE) \* rotate(-90deg, X)
The -90deg X rotation converts glTF Y-up to game Z-up. ISO_HEIGHT_SCALE
(~0.751) maintains isometric proportions.

---

Phase 5: Integration into TopoGame

File: src/game/topo_game.h — Add SkinnedRenderer skinned_renderer + bool
skinned_char_loaded

File: src/game/topo_game.cpp — In on_render_game():

1. Init skinned_renderer alongside rig_renderer (after terrain_renderer.init)
2. Load character GLB + animation GLBs (one-time, like gltf_column_loaded
    pattern)
3. Per frame: skinned_renderer.update(dt), then draw in a LOAD_PRESERVE_DEPTH
    pass after terrain (same slot as current rig_renderer)
4. Animation selection: threshold on player speed → idle/walk/run

File: src/game/topo_game.cpp — In on_cleanup(): add skinned_renderer.cleanup()

---

Phase 6: Transition Strategy

Keep both renderers during development. Add ImGui toggle:
ImGui::Checkbox("Use Skinned Character", &use_skinned);
Conditionally draw either rig_renderer or skinned_renderer. Once verified, the
procedural rig files can be removed.

---

Files to Create

- src/game/render/skeletal_animation.h — runtime bone palette computation
- src/game/render/skeletal_animation.cpp
- src/game/render/skinned_renderer.h — GPU skinned mesh renderer
- src/game/render/skinned_renderer.cpp
- src/shaders/skinned_character.vert.glsl — LBS vertex shader
- src/shaders/skinned_character.frag.glsl — lighting fragment shader

Files to Modify

- src/engine/core/gltf_loader.h — add SkinnedVertex, GltfSkeleton,
  GltfAnimationClip types
- src/engine/core/gltf_loader.cpp — implement load_gltf_skinned()
- src/game/topo_game.h — add SkinnedRenderer member
- src/game/topo_game.cpp — init/load/update/draw integration
- CMakeLists.txt — add new shader sources + cpp files

Existing Code to Reuse

- gpu_upload_buffer() from src/engine/gpu/gpu.h — static VBO/IBO upload
- AssetManager::load_shader() from src/engine/core/asset_manager.h — shader
  loading + hot-reload
- begin_render_pass_load_preserve_depth() from
  src/game/terrain/terrain_renderer.h — render pass that preserves terrain depth
- SceneUniforms + compute_uniforms() from terrain pipeline — shared
  camera/lighting uniforms
- Pipeline creation pattern from
  terrain_renderer.cpp::init_instanced_pipeline()

---

Verification

1. Build: cmake --build build -j$(nproc) — must compile with no errors
2. Visual: Run ./build/topogen, toggle "Use Skinned Character" in ImGui

- Character should appear at player position, upright, on terrain
- Idle animation should play (verify bones are moving)
- Walk/run when moving (verify locomotion clips work)

1. Depth: Character should be occluded by terrain in front, occlude terrain
    behind (depth test working)
2. Lighting: Character should receive same directional + point light shading
    as terrain
