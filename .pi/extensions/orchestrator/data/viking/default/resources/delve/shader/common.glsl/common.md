#ifndef COMMON_GLSL
#define COMMON_GLSL

#define UNI_TIME            params1.x
#define UNI_CONTOUR_OPACITY params1.y
#define UNI_LAVA_COLOR      lava_color.rgb
#define UNI_STAR_COLOR      star_light.rgb
#define UNI_STAR_INTENSITY  star_light.w
#define UNI_LIGHT_DIR       light_dir.xyz
#define UNI_AMBIENT         light_dir.w
#define UNI_LIGHT_COLOR     light_col.rgb
#define UNI_NEAR            depth_params.x
#define UNI_FAR             depth_params.y

#endif
