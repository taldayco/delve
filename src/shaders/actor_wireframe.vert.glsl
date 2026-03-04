#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in float bone_index0;
layout(location = 3) in float bone_weight;
layout(location = 4) in float bone_index1;

layout(set = 1, binding = 0) uniform SceneUniforms {
    mat4 view;
    mat4 projection;
    float time;
    float contour_opacity;
    float hex_border_width;
    float _pad0;
    float lava_color_r;
    float lava_color_g;
    float lava_color_b;
    float _pad1;
    float star_light_r;
    float star_light_g;
    float star_light_b;
    float star_light_intensity;
    float light_dir_x;
    float light_dir_y;
    float light_dir_z;
    float ambient;
    float light_col_r;
    float light_col_g;
    float light_col_b;
    float _pad2;
    float grid_size_x;
    float grid_size_y;
    float num_slices;
    float tile_px;
    float near_plane;
    float far_plane;
    float light_count_f;
    float _pad4;
} scene;

void main() {
    gl_Position = scene.projection * scene.view * vec4(position, 1.0);
}