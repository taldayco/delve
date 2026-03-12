#include "joint_mapping.h"
#include <SDL3/SDL_log.h>
#include <unordered_map>
#include <string>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

// --- Rigify DEF-bone name table ---
// ROOT and HEAD_END have no DEF- equivalent (derived joints).
struct RigifyEntry { Joint joint; const char *name; const char *alt; };
static const RigifyEntry RIGIFY_TABLE[] = {
    { Joint::HIPS,        "DEF-spine",           nullptr },
    { Joint::SPINE_01,    "DEF-spine.001",       nullptr },
    { Joint::SPINE_02,    "DEF-spine.002",       nullptr },
    { Joint::CHEST,       "DEF-spine.003",       nullptr },
    { Joint::NECK,        "DEF-spine.004",       nullptr },
    { Joint::HEAD,        "DEF-spine.005",       "DEF-head" },
    { Joint::L_CLAVICLE,  "DEF-shoulder.L",      nullptr },
    { Joint::L_UPPER_ARM, "DEF-upper_arm.L",     "DEF-upper_arm.L.001" },
    { Joint::L_LOWER_ARM, "DEF-forearm.L",       "DEF-forearm.L.001" },
    { Joint::L_HAND,      "DEF-hand.L",          nullptr },
    { Joint::R_CLAVICLE,  "DEF-shoulder.R",      nullptr },
    { Joint::R_UPPER_ARM, "DEF-upper_arm.R",     "DEF-upper_arm.R.001" },
    { Joint::R_LOWER_ARM, "DEF-forearm.R",       "DEF-forearm.R.001" },
    { Joint::R_HAND,      "DEF-hand.R",          nullptr },
    { Joint::L_UPPER_LEG, "DEF-thigh.L",         "DEF-thigh.L.001" },
    { Joint::L_LOWER_LEG, "DEF-shin.L",          "DEF-shin.L.001" },
    { Joint::L_FOOT,      "DEF-foot.L",          nullptr },
    { Joint::L_TOE,       "DEF-toe.L",           nullptr },
    { Joint::R_UPPER_LEG, "DEF-thigh.R",         "DEF-thigh.R.001" },
    { Joint::R_LOWER_LEG, "DEF-shin.R",          "DEF-shin.R.001" },
    { Joint::R_FOOT,      "DEF-foot.R",          nullptr },
    { Joint::R_TOE,       "DEF-toe.R",           nullptr },
};
static constexpr int RIGIFY_TABLE_SIZE = sizeof(RIGIFY_TABLE) / sizeof(RIGIFY_TABLE[0]);

// --- build_joint_mapping ---
bool build_joint_mapping(JointMapping &mapping,
                         const GltfSkinData &skin,
                         const SkeletonDef &skel,
                         const RigTransforms &bind_pose_xforms) {
    // Initialize index maps
    mapping.proc_to_gltf.fill(JOINT_UNMAPPED);
    mapping.gltf_joint_count = (uint32_t)skin.joints.size();
    uint32_t n = mapping.gltf_joint_count;
    mapping.gltf_to_proc.assign(n, JOINT_UNMAPPED);
    mapping.mapped_count = 0;
    mapping.valid = false;

    // Build name -> glTF index lookup
    std::unordered_map<std::string, int> name_to_idx;
    for (size_t i = 0; i < skin.joints.size(); ++i)
        name_to_idx[skin.joints[i].name] = (int)i;

    // Match each Rigify table entry
    for (int i = 0; i < RIGIFY_TABLE_SIZE; ++i) {
        const RigifyEntry &entry = RIGIFY_TABLE[i];
        int gltf_idx = JOINT_UNMAPPED;

        auto it = name_to_idx.find(entry.name);
        if (it != name_to_idx.end()) {
            gltf_idx = it->second;
        } else if (entry.alt) {
            it = name_to_idx.find(entry.alt);
            if (it != name_to_idx.end()) gltf_idx = it->second;
        }

        if (gltf_idx != JOINT_UNMAPPED) {
            mapping.proc_to_gltf[(int)entry.joint] = gltf_idx;
            mapping.gltf_to_proc[gltf_idx] = (int)entry.joint;
            mapping.mapped_count++;
            SDL_Log("JointMapping: %s -> glTF[%d] (%s)",
                    entry.name, gltf_idx, skin.joints[gltf_idx].name.c_str());
        } else {
            SDL_Log("JointMapping: UNMAPPED joint %d (%s)", (int)entry.joint, entry.name);
        }
    }

    mapping.valid = mapping.mapped_count >= 14;
    SDL_Log("JointMapping: %d/%d joints mapped (valid=%s)",
            mapping.mapped_count, RIGIFY_TABLE_SIZE, mapping.valid ? "yes" : "no");
    if (!mapping.valid) return false;

    // --- Precompute bind-pose data ---
    mapping.bind_global_rot.resize(n, glm::mat3(1.0f));
    mapping.bind_local_rot.resize(n, glm::mat3(1.0f));
    mapping.bind_local_translation.resize(n, glm::vec3(0.0f));
    mapping.rot_offset.resize(n, glm::mat3(1.0f));

    // Step 1: FK on glTF bind pose -> bind_global[i]
    std::vector<glm::mat4> bind_global(n, glm::mat4(1.0f));
    for (uint32_t i = 0; i < n; ++i) {
        glm::mat4 local_mat = joint_transform_to_mat4(skel.bind_pose[i]);
        int32_t parent = skel.parent_indices[i];
        bind_global[i] = (parent < 0) ? local_mat : bind_global[parent] * local_mat;
    }

    // Step 2: Extract per-joint bind data
    for (uint32_t i = 0; i < n; ++i) {
        mapping.bind_global_rot[i] = glm::mat3(bind_global[i]);
        mapping.bind_local_rot[i] = glm::mat3_cast(skel.bind_pose[i].rotation);
        mapping.bind_local_translation[i] = skel.bind_pose[i].translation;
    }

    // Step 3: Compute rot_offset for each MAPPED joint
    // rot_offset[i] = inverse(proc_bind_global_rot) * gltf_bind_global_rot
    for (uint32_t i = 0; i < n; ++i) {
        int proc_idx = mapping.gltf_to_proc[i];
        if (proc_idx == JOINT_UNMAPPED) continue;
        if (proc_idx >= (int)Joint::POLE_KNEE_L) continue;

        glm::mat3 proc_bind_rot = glm::mat3(bind_pose_xforms.bones[proc_idx]);
        // For orthonormal rotation matrices: inverse = transpose
        mapping.rot_offset[i] = glm::transpose(proc_bind_rot) * mapping.bind_global_rot[i];
    }

    SDL_Log("JointMapping: bind-pose calibration complete (%u joints, %d mapped)", n, mapping.mapped_count);
    return true;
}

// --- compute_skinning_palette: Global Rotation Retargeting ---
void compute_skinning_palette(
    const RigTransforms &xforms,
    const JointMapping &mapping,
    const SkeletonDef &skel,
    std::vector<glm::mat4> &palette)
{
    uint32_t n = mapping.gltf_joint_count;
    palette.resize(n);

    std::vector<glm::mat3> global_rot(n, glm::mat3(1.0f));
    std::vector<glm::vec3> global_pos(n, glm::vec3(0.0f));

    // Root-to-leaf traversal (parent_indices guarantee topological order)
    for (uint32_t i = 0; i < n; ++i) {
        int proc_idx = mapping.gltf_to_proc[i];
        int32_t parent = skel.parent_indices[i];

        // -- Rotation --
        if (proc_idx != JOINT_UNMAPPED && proc_idx < (int)Joint::POLE_KNEE_L) {
            // MAPPED: override global rotation from procedural rig + calibration offset
            glm::mat3 proc_global_rot = glm::mat3(xforms.bones[proc_idx]);
            global_rot[i] = proc_global_rot * mapping.rot_offset[i];
        } else {
            // UNMAPPED: inherit parent rotation, apply bind-pose local rotation
            if (parent >= 0) {
                global_rot[i] = global_rot[parent] * mapping.bind_local_rot[i];
            } else {
                global_rot[i] = mapping.bind_local_rot[i];
            }
        }

        // -- Position (always from glTF local translations = mesh bone lengths) --
        if (parent >= 0) {
            global_pos[i] = global_pos[parent]
                          + global_rot[parent] * mapping.bind_local_translation[i];
        } else {
            // Root joint: anchor to procedural rig position if mapped
            if (proc_idx != JOINT_UNMAPPED && proc_idx < (int)Joint::POLE_KNEE_L) {
                glm::vec3 proc_pos = glm::vec3(xforms.bones[proc_idx][3]);
                global_pos[i] = proc_pos;
            } else {
                global_pos[i] = mapping.bind_local_translation[i];
            }
        }

        // -- Assemble global mat4 --
        glm::mat4 g(1.0f);
        g[0] = glm::vec4(global_rot[i][0], 0.0f);
        g[1] = glm::vec4(global_rot[i][1], 0.0f);
        g[2] = glm::vec4(global_rot[i][2], 0.0f);
        g[3] = glm::vec4(global_pos[i], 1.0f);

        // -- Skinning palette entry --
        palette[i] = g * skel.inverse_bind_matrices[i];
    }
}

// --- compute_procedural_bind_pose ---
// Reconstructs the procedural rig's rest-pose at origin with facing=0.
// Same logic as SkeletonFinaliseSystem + RigTransformSystem at zero velocity/phase.
RigTransforms compute_procedural_bind_pose(const ActorConfig &cfg) {
    using J = Joint;
    RigPose pose{};

    float facing = 0.0f;
    float fwd_x  =  cosf(facing), fwd_y  = sinf(facing);
    float rght_x = -sinf(facing), rght_y = cosf(facing);

    // Transform at origin
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;

    // HIPS derived upward from ground
    float leg_height = cfg.leg_len + cfg.shin_len;
    glm::vec3 hips(tx, ty, tz + leg_height);
    glm::vec3 spine01 = hips  + glm::vec3(0, 0, cfg.torso_len * 0.4f);
    glm::vec3 chest   = hips  + glm::vec3(0, 0, cfg.torso_len);
    glm::vec3 neck    = chest + glm::vec3(0, 0, cfg.neck_len);
    glm::vec3 head    = neck  + glm::vec3(0, 0, cfg.head_radius);

    pose.joints[(int)J::HIPS]     = hips;
    pose.joints[(int)J::SPINE_01] = spine01;
    pose.joints[(int)J::CHEST]    = chest;
    pose.joints[(int)J::NECK]     = neck;
    pose.joints[(int)J::HEAD]     = head;

    // Hip sockets
    float hip_z = tz + leg_height;
    glm::vec3 l_upper_leg(tx - rght_x * cfg.hip_width, ty - rght_y * cfg.hip_width, hip_z);
    glm::vec3 r_upper_leg(tx + rght_x * cfg.hip_width, ty + rght_y * cfg.hip_width, hip_z);
    pose.joints[(int)J::L_UPPER_LEG] = l_upper_leg;
    pose.joints[(int)J::R_UPPER_LEG] = r_upper_leg;

    // Shoulder sockets (chest_facing = facing at rest)
    glm::vec3 l_upper_arm(chest.x - rght_x * cfg.shoulder_width,
                          chest.y - rght_y * cfg.shoulder_width, chest.z);
    glm::vec3 r_upper_arm(chest.x + rght_x * cfg.shoulder_width,
                          chest.y + rght_y * cfg.shoulder_width, chest.z);
    pose.joints[(int)J::L_UPPER_ARM] = l_upper_arm;
    pose.joints[(int)J::R_UPPER_ARM] = r_upper_arm;

    // Arms hanging straight down at rest
    glm::vec3 hang_down(0.0f, 0.0f, -1.0f);
    pose.joints[(int)J::L_LOWER_ARM] = l_upper_arm + hang_down * cfg.arm_len;
    pose.joints[(int)J::L_HAND]      = l_upper_arm + hang_down * (cfg.arm_len + cfg.forearm_len);
    pose.joints[(int)J::R_LOWER_ARM] = r_upper_arm + hang_down * cfg.arm_len;
    pose.joints[(int)J::R_HAND]      = r_upper_arm + hang_down * (cfg.arm_len + cfg.forearm_len);

    // Legs straight down at rest (feet at ground level)
    glm::vec3 l_foot(l_upper_leg.x, l_upper_leg.y, tz);
    glm::vec3 r_foot(r_upper_leg.x, r_upper_leg.y, tz);
    glm::vec3 l_lower_leg(l_upper_leg.x, l_upper_leg.y, tz + cfg.shin_len);
    glm::vec3 r_lower_leg(r_upper_leg.x, r_upper_leg.y, tz + cfg.shin_len);
    pose.joints[(int)J::L_LOWER_LEG] = l_lower_leg;
    pose.joints[(int)J::L_FOOT]      = l_foot;
    pose.joints[(int)J::R_LOWER_LEG] = r_lower_leg;
    pose.joints[(int)J::R_FOOT]      = r_foot;

    // Derived joints
    pose.joints[(int)J::ROOT] = glm::vec3(tx, ty, tz);
    pose.joints[(int)J::SPINE_02] = glm::mix(spine01, chest, 0.6f);
    pose.joints[(int)J::HEAD_END] = head + glm::vec3(0.0f, 0.0f, cfg.head_radius);
    pose.joints[(int)J::L_CLAVICLE] = glm::mix(chest, l_upper_arm, 0.25f);
    pose.joints[(int)J::R_CLAVICLE] = glm::mix(chest, r_upper_arm, 0.25f);

    glm::vec3 facing_dir(fwd_x, fwd_y, 0.0f);
    pose.joints[(int)J::L_TOE] = l_foot + facing_dir * cfg.toe_len;
    pose.joints[(int)J::R_TOE] = r_foot + facing_dir * cfg.toe_len;

    // Pole targets + IK goals (zero-motion defaults)
    glm::vec3 fwd3(fwd_x, fwd_y, 0.0f);
    pose.joints[(int)J::POLE_KNEE_L]  = l_lower_leg + fwd3 * 0.25f;
    pose.joints[(int)J::POLE_KNEE_R]  = r_lower_leg + fwd3 * 0.25f;
    pose.joints[(int)J::POLE_ELBOW_L] = pose.joints[(int)J::L_LOWER_ARM] - fwd3 * 0.25f;
    pose.joints[(int)J::POLE_ELBOW_R] = pose.joints[(int)J::R_LOWER_ARM] - fwd3 * 0.25f;
    pose.joints[(int)J::IK_FOOT_L] = l_foot;
    pose.joints[(int)J::IK_FOOT_R] = r_foot;
    pose.joints[(int)J::IK_HAND_L] = pose.joints[(int)J::L_HAND];
    pose.joints[(int)J::IK_HAND_R] = pose.joints[(int)J::R_HAND];

    // Now build bone transforms (same as RigTransformSystem)
    RigTransforms xforms{};
    const auto &jt = pose.joints;

    glm::vec3 vis_fwd(fwd_x, fwd_y, 0.0f);
    glm::vec3 vis_rght(rght_x, rght_y, 0.0f);
    glm::vec3 chest_fwd = vis_fwd;   // at rest, chest_facing = visual_facing
    glm::vec3 chest_rght = vis_rght;

    // ROOT
    xforms.bones[(int)J::ROOT] = make_bone_mat4(
        vis_rght, vis_fwd, glm::vec3(0.0f, 0.0f, 1.0f), jt[(int)J::ROOT]);

    // Spine chain
    struct SpineEntry { Joint self; Joint child; float chest_blend; };
    static constexpr SpineEntry spine_chain[] = {
        { J::HIPS,     J::SPINE_01, 0.0f },
        { J::SPINE_01, J::SPINE_02, 0.0f },
        { J::SPINE_02, J::CHEST,    0.5f },
        { J::CHEST,    J::NECK,     1.0f },
        { J::NECK,     J::HEAD,     1.0f },
    };
    for (const auto &e : spine_chain) {
        glm::vec3 bone_dir = jt[(int)e.child] - jt[(int)e.self];
        float blen = glm::length(bone_dir);
        if (blen < 1e-5f) {
            glm::mat4 m(1.0f);
            m[3] = glm::vec4(jt[(int)e.self], 1.0f);
            xforms.bones[(int)e.self] = m;
            continue;
        }
        glm::vec3 ref = glm::mix(vis_fwd, chest_fwd, e.chest_blend);
        float ref_len = glm::length(ref);
        if (ref_len < 1e-5f) ref = vis_fwd;
        else ref /= ref_len;

        glm::vec3 right, fwd, up;
        build_bone_basis(bone_dir, ref, right, fwd, up);
        xforms.bones[(int)e.self] = make_bone_mat4(right, fwd, up, jt[(int)e.self]);
    }

    // HEAD
    {
        glm::vec3 bone_dir = jt[(int)J::HEAD_END] - jt[(int)J::HEAD];
        float blen = glm::length(bone_dir);
        if (blen < 1e-5f) {
            glm::mat4 m(1.0f); m[3] = glm::vec4(jt[(int)J::HEAD], 1.0f);
            xforms.bones[(int)J::HEAD] = m;
        } else {
            glm::vec3 right, fwd, up;
            build_bone_basis(bone_dir, chest_fwd, right, fwd, up);
            xforms.bones[(int)J::HEAD] = make_bone_mat4(right, fwd, up, jt[(int)J::HEAD]);
        }
    }
    // HEAD_END
    {
        glm::mat4 m = xforms.bones[(int)J::HEAD];
        m[3] = glm::vec4(jt[(int)J::HEAD_END], 1.0f);
        xforms.bones[(int)J::HEAD_END] = m;
    }

    // Limb chain helper
    auto compute_limb = [&](Joint upper, Joint lower, Joint end,
                            Joint toe, const glm::vec3 &fallback_perp, bool is_leg) {
        glm::vec3 upper_dir = jt[(int)lower] - jt[(int)upper];
        glm::vec3 lower_dir = jt[(int)end]   - jt[(int)lower];
        float upper_len = glm::length(upper_dir);
        float lower_len = glm::length(lower_dir);

        glm::vec3 bend_normal = fallback_perp;
        if (upper_len > 1e-5f && lower_len > 1e-5f) {
            glm::vec3 bn = glm::cross(upper_dir / upper_len, lower_dir / lower_len);
            float bn_len = glm::length(bn);
            if (bn_len > 1e-5f) bend_normal = bn / bn_len;
        }

        if (upper_len > 1e-5f) {
            glm::vec3 up = upper_dir / upper_len;
            glm::vec3 right = bend_normal;
            glm::vec3 fwd = glm::cross(up, right);
            right = glm::cross(fwd, up);
            xforms.bones[(int)upper] = make_bone_mat4(right, fwd, up, jt[(int)upper]);
        } else {
            glm::mat4 m(1.0f); m[3] = glm::vec4(jt[(int)upper], 1.0f);
            xforms.bones[(int)upper] = m;
        }

        if (lower_len > 1e-5f) {
            glm::vec3 up = lower_dir / lower_len;
            glm::vec3 right = bend_normal - glm::dot(bend_normal, up) * up;
            float rlen = glm::length(right);
            if (rlen < 1e-5f) {
                right = fallback_perp - glm::dot(fallback_perp, up) * up;
                rlen = glm::length(right);
                if (rlen < 1e-5f) {
                    glm::vec3 alt = (std::abs(up.z) < 0.9f)
                        ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
                    right = glm::normalize(glm::cross(alt, up));
                    rlen = 1.0f;
                }
            }
            right /= rlen;
            glm::vec3 fwd = glm::cross(up, right);
            xforms.bones[(int)lower] = make_bone_mat4(right, fwd, up, jt[(int)lower]);
        } else {
            glm::mat4 m(1.0f); m[3] = glm::vec4(jt[(int)lower], 1.0f);
            xforms.bones[(int)lower] = m;
        }

        if (is_leg) {
            glm::vec3 toe_dir = jt[(int)toe] - jt[(int)end];
            float toe_len = glm::length(toe_dir);
            glm::vec3 fwd = (toe_len > 1e-5f) ? toe_dir / toe_len : vis_fwd;
            glm::vec3 up(0.0f, 0.0f, 1.0f);
            glm::vec3 right = glm::cross(fwd, up);
            float rlen = glm::length(right);
            if (rlen < 1e-5f) right = vis_rght;
            else right /= rlen;
            up = glm::cross(right, fwd);
            xforms.bones[(int)end] = make_bone_mat4(right, fwd, up, jt[(int)end]);
            glm::mat4 m = xforms.bones[(int)end];
            m[3] = glm::vec4(jt[(int)toe], 1.0f);
            xforms.bones[(int)toe] = m;
        } else {
            if (lower_len > 1e-5f) {
                glm::mat4 m = xforms.bones[(int)lower];
                m[3] = glm::vec4(jt[(int)end], 1.0f);
                xforms.bones[(int)end] = m;
            } else {
                glm::mat4 m(1.0f); m[3] = glm::vec4(jt[(int)end], 1.0f);
                xforms.bones[(int)end] = m;
            }
        }
    };

    compute_limb(J::L_UPPER_LEG, J::L_LOWER_LEG, J::L_FOOT, J::L_TOE,
        glm::vec3(-vis_rght.x, -vis_rght.y, 0.0f), true);
    compute_limb(J::R_UPPER_LEG, J::R_LOWER_LEG, J::R_FOOT, J::R_TOE,
        glm::vec3(vis_rght.x, vis_rght.y, 0.0f), true);
    compute_limb(J::L_UPPER_ARM, J::L_LOWER_ARM, J::L_HAND, J::L_HAND,
        glm::vec3(-chest_fwd.x, -chest_fwd.y, 0.0f), false);
    compute_limb(J::R_UPPER_ARM, J::R_LOWER_ARM, J::R_HAND, J::R_HAND,
        glm::vec3(-chest_fwd.x, -chest_fwd.y, 0.0f), false);

    // Clavicles
    auto compute_clavicle = [&](Joint clav, Joint upper_arm, const glm::vec3 &ref) {
        glm::vec3 bone_dir = jt[(int)upper_arm] - jt[(int)clav];
        float blen = glm::length(bone_dir);
        if (blen < 1e-5f) {
            glm::mat4 m(1.0f); m[3] = glm::vec4(jt[(int)clav], 1.0f);
            xforms.bones[(int)clav] = m;
        } else {
            glm::vec3 right, fwd, up;
            build_bone_basis(bone_dir, ref, right, fwd, up);
            xforms.bones[(int)clav] = make_bone_mat4(right, fwd, up, jt[(int)clav]);
        }
    };
    compute_clavicle(J::L_CLAVICLE, J::L_UPPER_ARM, chest_fwd);
    compute_clavicle(J::R_CLAVICLE, J::R_UPPER_ARM, -chest_fwd);

    // IK virtual joints
    static constexpr Joint ik_joints[] = {
        J::POLE_KNEE_L, J::POLE_KNEE_R, J::POLE_ELBOW_L, J::POLE_ELBOW_R,
        J::IK_FOOT_L, J::IK_FOOT_R, J::IK_HAND_L, J::IK_HAND_R,
    };
    for (Joint j : ik_joints) {
        glm::mat4 m(1.0f); m[3] = glm::vec4(jt[(int)j], 1.0f);
        xforms.bones[(int)j] = m;
    }

    return xforms;
}
