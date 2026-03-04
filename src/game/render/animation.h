#pragma once
#include "actor.h"
#include <glm/glm.hpp>

// Compute a walk-cycle skeleton pose for a character at `hip` world position.
// walk_phase: continuously advancing phase in radians (caller advances by dt * speed * 2π / stride).
// Amplitudes: leg_swing=0.55 rad, knee_bend=0.65 rad, ankle dorsiflexion=0.20 rad.
// Returns joint positions already in world space — no FK propagation step needed.
SkeletonPose compute_walk_pose(const ActorConfig &c, float walk_phase, glm::vec3 hip);

// Compute an idle (standing) skeleton pose for a character at `hip` world position.
// time: free-running time in seconds, used for breathing and micro-sway motion.
// Returns joint positions already in world space — no FK propagation step needed.
SkeletonPose compute_idle_pose(const ActorConfig &c, float time, glm::vec3 hip);
