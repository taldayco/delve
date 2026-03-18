layout(set = 1, binding = 0) uniform SceneUniforms {
    mat4 view;
    mat4 projection;
    vec4 params1;
    vec4 lava_color;
    vec4 star_light;
    vec4 light_dir;
    vec4 light_col;
    vec4 grid_params;
    vec4 depth_params;
    vec4 cam_world_pos;
    vec4 view_dir_ws;
};

#define TIME              params1.x
#define CONTOUR_OPACITY   params1.y
#define HEX_BORDER_WIDTH  params1.z
#define NEAR_PLANE        depth_params.x
#define FAR_PLANE         depth_params.y
#define LIGHT_COUNT       uint(depth_params.z)

#ifdef COORD_FRAGMENT_STAGE

uint cluster_index() {
    float numSlices = grid_params.z;
    float tilePx    = grid_params.w;
    uint  gridX     = uint(grid_params.x);
    uint  gridY     = uint(grid_params.y);

    uint zSlice = clamp(uint(gl_FragCoord.z * numSlices), 0u, uint(numSlices) - 1u);
    uint tileX  = clamp(uint(gl_FragCoord.x / tilePx), 0u, gridX - 1u);
    uint tileY  = clamp(uint(gl_FragCoord.y / tilePx), 0u, gridY - 1u);

    return tileX + tileY * gridX + zSlice * gridX * gridY;
}

#endif
