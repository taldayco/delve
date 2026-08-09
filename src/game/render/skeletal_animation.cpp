#include "skeletal_animation.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

static glm::mat4 local_to_mat4(const BoneLocalTransform &xf) {
    glm::mat4 T = glm::translate(glm::mat4(1.f), xf.translation);
    glm::mat4 R = glm::mat4_cast(xf.rotation);
    glm::mat4 S = glm::scale(glm::mat4(1.f), xf.scale);
    return T * R * S;
}

BonePalette compute_bone_palette(const GltfSkeleton &skel,
                                  const std::vector<BoneLocalTransform> &local_transforms,
                                  const glm::mat4 &root_transform) {
    BonePalette palette{};
    int num_bones = (int)skel.bones.size();
    if (num_bones == 0) return palette;

    num_bones = std::min(num_bones, 65);

    std::vector<glm::mat4> global(num_bones, glm::mat4(1.f));

    for (int i = 0; i < num_bones; ++i) {
        glm::mat4 local = (i < (int)local_transforms.size())
            ? local_to_mat4(local_transforms[i])
            : skel.bones[i].local_rest_transform;

        int parent = skel.bones[i].parent_index;
        if (parent < 0 || parent >= num_bones) {
            global[i] = skel.armature_transform * local;
        } else {
            global[i] = global[parent] * local;
        }

        palette.bones[i] = root_transform * global[i] * skel.bones[i].inverse_bind_matrix;
    }

    return palette;
}

BoneLocalTransform rest_pose_local(const glm::mat4 &m) {
    BoneLocalTransform xf;
    xf.translation = glm::vec3(m[3]);
    glm::vec3 sx(m[0]), sy(m[1]), sz(m[2]);
    xf.scale = glm::vec3(glm::length(sx), glm::length(sy), glm::length(sz));
    if (xf.scale.x > 1e-6f && xf.scale.y > 1e-6f && xf.scale.z > 1e-6f) {
        glm::mat3 rot(sx / xf.scale.x, sy / xf.scale.y, sz / xf.scale.z);
        xf.rotation = glm::quat_cast(rot);
    }
    return xf;
}

// Clamp-and-lerp between the keyframe pair bracketing `time`.
template <typename T, typename Mix>
static T sample_keyframes(const std::vector<float> &times, const std::vector<T> &values,
                          float time, Mix mix) {
    int n = (int)times.size();
    if (n == 1 || time <= times[0]) return values[0];
    if (time >= times[n - 1]) return values[n - 1];
    int hi = 1;
    while (hi < n && times[hi] < time) ++hi;
    int lo = hi - 1;
    float denom = times[hi] - times[lo];
    float t = denom > 0.f ? (time - times[lo]) / denom : 0.f;
    return mix(values[lo], values[hi], t);
}

void AnimationMixer::sample_clip(const GltfAnimationClip *clip, float time,
                                 std::vector<BoneLocalTransform> &out) {
    if (!clip) return;
    for (const auto &ch : clip->channels) {
        if (ch.bone_index < 0 || ch.bone_index >= (int)out.size()) continue;
        if (ch.times.empty()) continue;
        BoneLocalTransform &xf = out[ch.bone_index];

        if (ch.path == "translation") {
            xf.translation = sample_keyframes(ch.times, ch.translations, time,
                [](const glm::vec3 &a, const glm::vec3 &b, float t) { return glm::mix(a, b, t); });
        } else if (ch.path == "rotation") {
            xf.rotation = sample_keyframes(ch.times, ch.rotations, time,
                [](const glm::quat &a, const glm::quat &b, float t) { return glm::slerp(a, b, t); });
        } else if (ch.path == "scale") {
            xf.scale = sample_keyframes(ch.times, ch.scales, time,
                [](const glm::vec3 &a, const glm::vec3 &b, float t) { return glm::mix(a, b, t); });
        }
    }
}

void AnimationMixer::set_clip(const GltfAnimationClip *clip, float crossfade_duration) {
    if (clip == current_clip_) return;
    if (current_clip_) {
        outgoing_clip_ = current_clip_;
        outgoing_time_ = current_time_;
    }
    current_clip_   = clip;
    current_time_   = 0.f;
    blend_alpha_    = 0.f;
    blend_duration_ = crossfade_duration;
    if (!outgoing_clip_) blend_alpha_ = 1.f;
}

void AnimationMixer::update(float dt) {
    float scaled_dt = dt * playback_speed_;

    if (current_clip_ && current_clip_->duration > 0.f) {
        current_time_ += scaled_dt;
        while (current_time_ >= current_clip_->duration)
            current_time_ -= current_clip_->duration;
        if (current_time_ < 0.f) current_time_ = 0.f;
    }

    if (outgoing_clip_ && outgoing_clip_->duration > 0.f) {
        outgoing_time_ += scaled_dt;
        while (outgoing_time_ >= outgoing_clip_->duration)
            outgoing_time_ -= outgoing_clip_->duration;
        if (outgoing_time_ < 0.f) outgoing_time_ = 0.f;
    }

    if (blend_duration_ > 0.f && blend_alpha_ < 1.f) {
        blend_alpha_ += dt / blend_duration_;
        if (blend_alpha_ >= 1.f) {
            blend_alpha_ = 1.f;
            outgoing_clip_ = nullptr;
        }
    }
}

void AnimationMixer::sample(const GltfSkeleton &skel, std::vector<BoneLocalTransform> &out) const {
    int num_bones = std::min((int)skel.bones.size(), (int)out.size());

    for (int i = 0; i < num_bones; ++i)
        out[i] = rest_pose_local(skel.bones[i].local_rest_transform);

    if (!current_clip_ && !outgoing_clip_) return;

    if (blend_alpha_ >= 1.f || !outgoing_clip_) {
        sample_clip(current_clip_, current_time_, out);
    } else {
        std::vector<BoneLocalTransform> outgoing_pose(out);
        std::vector<BoneLocalTransform> current_pose(out);

        sample_clip(outgoing_clip_, outgoing_time_, outgoing_pose);
        sample_clip(current_clip_, current_time_, current_pose);

        for (int i = 0; i < num_bones; ++i) {
            out[i].translation = glm::mix(outgoing_pose[i].translation,
                                           current_pose[i].translation, blend_alpha_);
            out[i].rotation    = glm::slerp(outgoing_pose[i].rotation,
                                              current_pose[i].rotation, blend_alpha_);
            out[i].scale       = glm::mix(outgoing_pose[i].scale,
                                           current_pose[i].scale, blend_alpha_);
        }
    }
}

float AnimationMixer::get_normalized_time() const {
    if (!current_clip_ || current_clip_->duration <= 0.f) return 0.f;
    return current_time_ / current_clip_->duration;
}
