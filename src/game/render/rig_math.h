#pragma once
#include "rig.h"
#include <glm/glm.hpp>

void solve_two_bone(glm::vec3 H, glm::vec3 target,
                    float a, float b,
                    glm::vec3 pole, glm::vec3 fallback_perp,
                    glm::vec3 &out_mid, glm::vec3 &out_end);

void compute_derived_joints(RigPose &pose, const ActorConfig &cfg,
                            float visual_facing, float chest_facing,
                            const glm::vec3 &root_pos);
