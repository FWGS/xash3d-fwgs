#extension GL_EXT_ray_tracing: require

#define PAYLOAD_LOCATION_OPAQUE 0
#define PAYLOAD_LOCATION_SHADOW 1
#define PAYLOAD_LOCATION_ADDITIVE 2

struct RayPayloadOpaque {
	float t_offset, pixel_cone_spread_angle;
	vec4 hit_pos_t;
	vec4 prev_pos_t;
	vec3 normal;
	vec3 geometry_normal;
	vec3 base_color;
	float transmissiveness;
	vec3 emissive;
	float roughness;
	float metalness;
	int kusok_index;
	uint material_index;
	vec4 debug;
};


#define SHADOW_MISS 0
#define SHADOW_HIT 1
#define SHADOW_SKY 2

struct RayPayloadShadow {
	uint hit_type;
};


const float additive_soft_overshoot = 16.;
struct RayPayloadAdditive {
	vec3 color;
	float ray_distance;
};
