#extension GL_EXT_ray_tracing: require
struct RayPayload {
    float t_offset, pixel_cone_spread_angle;
    vec4 hit_pos_t;
    vec3 albedo;
    vec3 normal;
    float roughness;
    int kusok_index;
    uint material_flags;
};