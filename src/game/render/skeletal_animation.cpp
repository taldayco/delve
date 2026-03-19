#include "skeletal_animation.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

void AnimationPlayer::set_clip(const GltfAnimationClip *clip) {
    clip_ = clip;
    time_ = 0.f;
}

void AnimationPlayer::update(float dt) {
    if (!clip_ || clip_->duration <= 0.f) return;
    time_ += dt;
    while (time_ >= clip_->duration)
        time_ -= clip_->duration;
    if (time_ < 0.f) time_ = 0.f;
}

void AnimationPlayer::sample(std::vector<BoneLocalTransform> &out) const {
    if (!clip_) return;

    for (const auto &ch : clip_->channels) {
        if (ch.bone_index < 0 || ch.bone_index >= (int)out.size()) continue;
        BoneLocalTransform &xf = out[ch.bone_index];

        if (ch.path == "translation" && !ch.times.empty()) {
            int n = (int)ch.times.size();
            if (n == 1 || time_ <= ch.times[0]) {
                xf.translation = ch.translations[0];
            } else if (time_ >= ch.times[n - 1]) {
                xf.translation = ch.translations[n - 1];
            } else {
                int hi = 1;
                while (hi < n && ch.times[hi] < time_) ++hi;
                int lo = hi - 1;
                float t = (time_ - ch.times[lo]) / (ch.times[hi] - ch.times[lo]);
                xf.translation = glm::mix(ch.translations[lo], ch.translations[hi], t);
            }
        } else if (ch.path == "rotation" && !ch.times.empty()) {
            int n = (int)ch.times.size();
            if (n == 1 || time_ <= ch.times[0]) {
                xf.rotation = ch.rotations[0];
            } else if (time_ >= ch.times[n - 1]) {
                xf.rotation = ch.rotations[n - 1];
            } else {
                int hi = 1;
                while (hi < n && ch.times[hi] < time_) ++hi;
                int lo = hi - 1;
                float t = (time_ - ch.times[lo]) / (ch.times[hi] - ch.times[lo]);
                xf.rotation = glm::slerp(ch.rotations[lo], ch.rotations[hi], t);
            }
        } else if (ch.path == "scale" && !ch.times.empty()) {
            int n = (int)ch.times.size();
            if (n == 1 || time_ <= ch.times[0]) {
                xf.scale = ch.scales[0];
            } else if (time_ >= ch.times[n - 1]) {
                xf.scale = ch.scales[n - 1];
            } else {
                int hi = 1;
                while (hi < n && ch.times[hi] < time_) ++hi;
                int lo = hi - 1;
                float t = (time_ - ch.times[lo]) / (ch.times[hi] - ch.times[lo]);
                xf.scale = glm::mix(ch.scales[lo], ch.scales[hi], t);
            }
        }
    }
}

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

// --- AnimationMixer ---

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

void AnimationMixer::sample_clip(const GltfAnimationClip *clip, float time,
                                  const GltfSkeleton &skel,
                                  std::vector<BoneLocalTransform> &out) const {
    if (!clip) return;
    // Use a temporary AnimationPlayer-style sample
    for (const auto &ch : clip->channels) {
        if (ch.bone_index < 0 || ch.bone_index >= (int)out.size()) continue;
        BoneLocalTransform &xf = out[ch.bone_index];

        if (ch.path == "translation" && !ch.times.empty()) {
            int n = (int)ch.times.size();
            if (n == 1 || time <= ch.times[0]) {
                xf.translation = ch.translations[0];
            } else if (time >= ch.times[n - 1]) {
                xf.translation = ch.translations[n - 1];
            } else {
                int hi = 1;
                while (hi < n && ch.times[hi] < time) ++hi;
                int lo = hi - 1;
                float t = (ch.times[hi] - ch.times[lo]) > 0.f
                    ? (time - ch.times[lo]) / (ch.times[hi] - ch.times[lo]) : 0.f;
                xf.translation = glm::mix(ch.translations[lo], ch.translations[hi], t);
            }
        } else if (ch.path == "rotation" && !ch.times.empty()) {
            int n = (int)ch.times.size();
            if (n == 1 || time <= ch.times[0]) {
                xf.rotation = ch.rotations[0];
            } else if (time >= ch.times[n - 1]) {
                xf.rotation = ch.rotations[n - 1];
            } else {
                int hi = 1;
                while (hi < n && ch.times[hi] < time) ++hi;
                int lo = hi - 1;
                float t = (ch.times[hi] - ch.times[lo]) > 0.f
                    ? (time - ch.times[lo]) / (ch.times[hi] - ch.times[lo]) : 0.f;
                xf.rotation = glm::slerp(ch.rotations[lo], ch.rotations[hi], t);
            }
        } else if (ch.path == "scale" && !ch.times.empty()) {
            int n = (int)ch.times.size();
            if (n == 1 || time <= ch.times[0]) {
                xf.scale = ch.scales[0];
            } else if (time >= ch.times[n - 1]) {
                xf.scale = ch.scales[n - 1];
            } else {
                int hi = 1;
                while (hi < n && ch.times[hi] < time) ++hi;
                int lo = hi - 1;
                float t = (ch.times[hi] - ch.times[lo]) > 0.f
                    ? (time - ch.times[lo]) / (ch.times[hi] - ch.times[lo]) : 0.f;
                xf.scale = glm::mix(ch.scales[lo], ch.scales[hi], t);
            }
        }
    }
}

void AnimationMixer::sample(const GltfSkeleton &skel, std::vector<BoneLocalTransform> &out) const {
    int num_bones = std::min((int)skel.bones.size(), (int)out.size());

    // Initialize to rest pose
    for (int i = 0; i < num_bones; ++i) {
        const glm::mat4 &m = skel.bones[i].local_rest_transform;
        out[i].translation = glm::vec3(m[3]);
        glm::vec3 sx(m[0]), sy(m[1]), sz(m[2]);
        out[i].scale = glm::vec3(glm::length(sx), glm::length(sy), glm::length(sz));
        if (out[i].scale.x > 1e-6f && out[i].scale.y > 1e-6f && out[i].scale.z > 1e-6f) {
            glm::mat3 rot(sx / out[i].scale.x, sy / out[i].scale.y, sz / out[i].scale.z);
            out[i].rotation = glm::quat_cast(rot);
        }
    }

    if (!current_clip_ && !outgoing_clip_) return;

    if (blend_alpha_ >= 1.f || !outgoing_clip_) {
        // Only current clip
        sample_clip(current_clip_, current_time_, skel, out);
    } else {
        // Blend outgoing → current
        std::vector<BoneLocalTransform> outgoing_pose(out);
        std::vector<BoneLocalTransform> current_pose(out);

        sample_clip(outgoing_clip_, outgoing_time_, skel, outgoing_pose);
        sample_clip(current_clip_, current_time_, skel, current_pose);

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

// --- SkeletalAnimation ---

void SkeletalAnimation::init(const GltfSkinnedAsset &asset) {
    skeleton_              = asset.skeleton;
    inverse_bind_matrices_ = asset.inverse_bind_matrices;
    clips_                 = asset.animations;

    int n = std::min((int)skeleton_.bones.size(), (int)inverse_bind_matrices_.size());
    for (int i = 0; i < n; ++i) {
        skeleton_.bones[i].inverse_bind_matrix = inverse_bind_matrices_[i];
    }

    std::memset(&bone_palette_, 0, sizeof(bone_palette_));
    int num_bones = std::min((int)skeleton_.bones.size(), 65);
    for (int i = 0; i < num_bones; ++i)
        bone_palette_.bones[i] = glm::mat4(1.f);

    if (!clips_.empty()) {
        player_.set_clip(&clips_[0]);
    }
}

bool SkeletalAnimation::play(const std::string &clip_name) {
    for (auto &clip : clips_) {
        if (clip.name == clip_name) {
            player_.set_clip(&clip);
            return true;
        }
    }
    return false;
}

void SkeletalAnimation::update(float dt, const glm::mat4 &root_transform) {
    if (skeleton_.bones.empty()) return;

    player_.update(dt);

    int num_bones = std::min((int)skeleton_.bones.size(), 65);
    std::vector<BoneLocalTransform> locals(num_bones);
    for (int i = 0; i < num_bones; ++i) {
        const glm::mat4 &m = skeleton_.bones[i].local_rest_transform;
        locals[i].translation = glm::vec3(m[3]);
        glm::vec3 sx(m[0]), sy(m[1]), sz(m[2]);
        locals[i].scale = glm::vec3(glm::length(sx), glm::length(sy), glm::length(sz));
        glm::mat3 rot(sx / locals[i].scale.x, sy / locals[i].scale.y, sz / locals[i].scale.z);
        locals[i].rotation = glm::quat_cast(rot);
    }

    player_.sample(locals);

    bone_palette_ = compute_bone_palette(skeleton_, locals, root_transform);
}
