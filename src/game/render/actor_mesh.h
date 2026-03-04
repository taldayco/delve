#pragma once
#include "actor.h"
#include "render/skeleton_mesh.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

// Minimal per-vertex layout for the actor wireframe: world-space position + RGBA color.
// Submitted as a line-list (each consecutive pair of vertices = one edge).
struct ActorMeshVertex {
    glm::vec3 position;  // world-space (CPU-skinned each frame)
    glm::vec4 color;     // always {1,1,1,1} for the wireframe path
};
static_assert(sizeof(ActorMeshVertex) == 28, "ActorMeshVertex must be 28 bytes");

// CPU-side wireframe mesh for a skeletal actor.
// vertices: flat line-list pairs — vertex[2i] and vertex[2i+1] form edge i.
// No index buffer; draw with DrawPrimitives(vertex_count / 2 lines).
struct ActorMesh {
    std::vector<ActorMeshVertex> vertices; // line-list pairs, count always even
};

// Build a white wireframe ActorMesh from the given pose + per-bone profiles.
// For each bone segment the polygon ring at each end and the longitudinal edges
// are emitted as line-list pairs.  All vertex colors are {1,1,1,1}.
ActorMesh generate_actor_wireframe_mesh(const SkeletonPose     &pose,
                                        const BoneProfileArray &profiles);

// Re-skin an existing ActorMesh in-place: recompute world-space vertex positions
// from the current pose without reallocating the vertex vector.
void deform_actor_mesh(ActorMesh &mesh, const SkeletonPose &pose,
                       const BoneProfileArray &profiles);