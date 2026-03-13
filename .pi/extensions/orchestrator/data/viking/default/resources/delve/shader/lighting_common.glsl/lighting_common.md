





struct PointLight {
    vec4 positionRadius;
    vec4 colorIntensity;
};

struct ClusterAABB {
    vec4 minPoint;
    vec4 maxPoint;
};

struct LightGridEntry {
    uint offset;
    uint count;
};
