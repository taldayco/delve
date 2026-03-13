#version 450



layout(local_size_x = 16, local_size_y = 9, local_size_z = 1) in;



layout(set = 1, binding = 0) uniform ClusterUniforms {
    float tile_px;
    float grid_size_x;
    float grid_size_y;
    float num_slices;
    float near_plane;
    float far_plane;
    float screen_w;
    float screen_h;
    float _pad0, _pad1;
};


struct ClusterAABB {
    vec4 minPoint;
    vec4 maxPoint;
};

layout(set = 0, binding = 0) buffer ClusterBuffer {
    ClusterAABB clusters[];
};





vec3 screenToView(vec2 screenPos) {

    vec2 ndc = (screenPos / vec2(screen_w, screen_h)) * 2.0 - 1.0;
    return vec3(ndc, 0.0);
}


float sliceNear(float k) {
    return k / num_slices;
}

void main() {
    uvec3 id = gl_GlobalInvocationID;


    if (id.x >= uint(grid_size_x) || id.y >= uint(grid_size_y) || id.z >= uint(num_slices))
        return;


    vec2 tileMin = vec2(id.xy) * tile_px;
    vec2 tileMax = tileMin + tile_px;


    vec3 vMin = screenToView(tileMin);
    vec3 vMax = screenToView(tileMax);


    float zNear = sliceNear(float(id.z));
    float zFar  = sliceNear(float(id.z + 1));


    vec3 aabbMin = vec3(min(vMin.x, vMax.x), min(vMin.y, vMax.y), zNear);
    vec3 aabbMax = vec3(max(vMin.x, vMax.x), max(vMin.y, vMax.y), zFar);

    uint gridX   = uint(grid_size_x);
    uint gridY   = uint(grid_size_y);
    uint idx     = id.x + id.y * gridX + id.z * gridX * gridY;

    clusters[idx].minPoint = vec4(aabbMin, 0.0);
    clusters[idx].maxPoint = vec4(aabbMax, 0.0);
}
