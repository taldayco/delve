---
name: test
description: Write and maintain quantitative visual tests
when: When writing tests, adding metric extractors, or verifying visual properties
---

# Test Engineer Skill

You write and maintain quantitative visual tests that measure and assert properties of the terrain, animation, geometry, and rendering systems.

## Philosophy
This project is visual. Traditional unit tests miss "does it look right?" Instead, we quantify every visual property and assert measurable ranges. No 3rd party test frameworks — everything is custom.

## Test Infrastructure

### Test Harness (`src/test/test_harness.h/cpp`)
```cpp
// Register a test
DELVE_TEST(test_name) {
    // ... test body ...
    EXPECT_TRUE(condition);
    EXPECT_NEAR(actual, expected, epsilon);
    EXPECT_RANGE(value, min, max);
    EXPECT_GT(a, b);
    EXPECT_LT(a, b);
}
```

Run: `cmake --build build --target delve_tests && ./build/delve_tests`
Output: JSON to stdout for agent consumption.

### Metric Extractors
Functions that quantify visual properties. Located in `src/test/`:

**Terrain Metrics** (`terrain_metrics.h/cpp`):
- `elevation_distribution(MapData&)` → histogram of elevation bands
- `water_coverage_percent(MapData&)` → % below water threshold
- `plateau_count(ContourData&)` → number of distinct regions
- `hex_column_height_range(columns)` → {min, max, mean}
- `lava_body_count(MapData&)` → number of lava regions
- `void_region_ratio(MapData&)` → % of map that is void

**Geometry Metrics** (`geometry_metrics.h/cpp`):
- `mesh_vertex_count(TerrainMesh&)` → total vertices
- `mesh_degenerate_triangles(TerrainMesh&)` → zero-area triangle count
- `hex_grid_regularity(coords)` → proper tiling check
- `normal_validity(TerrainMesh&)` → % of unit-length normals

**Animation Metrics** (`animation_metrics.h/cpp`):
- `joint_angle(SkeletonPose&, joint)` → degrees
- `joint_facing_direction(SkeletonPose&, joint)` → unit vector
- `gait_stride_length(ProceduralGait&)` → world units
- `arm_swing_amplitude(ProceduralGait&)` → degrees
- `pose_symmetry_score(SkeletonPose&)` → 0.0-1.0

## Writing a New Test

1. Identify the visual property to verify
2. Check if a metric extractor already exists; if not, write one in the appropriate `*_metrics.cpp` file
3. Write the test case in `src/test/tests/test_<subsystem>.cpp`:
   ```cpp
   DELVE_TEST(terrain_has_valid_elevation_range) {
       MapData map;
       map.allocate(256, 256);  // Use smaller map for test speed
       ElevationParams params;
       params.seed = 42;  // Deterministic seed
       generate_elevation_layer(map.elevation, 256, 256, params);

       for (float e : map.elevation) {
           EXPECT_RANGE(e, 0.0f, 1.0f);
       }
   }
   ```
4. Add the source file to CMakeLists.txt under `delve_tests` target

## Test Conventions
- Use deterministic seeds for reproducibility
- Use smaller maps (256x256) in tests for speed — the 1024x1024 default is too slow
- Test one property per test function
- Name tests: `subsystem_property_being_tested`
- Metric extractors are pure functions — no side effects, no GPU dependency
- Tests must not require a GPU or window — test data structures and algorithms only
