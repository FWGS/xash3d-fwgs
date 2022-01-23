#define RAY_LIGHT_DIRECT_INPUTS(X) \
	X(10, position_t, rgba32f) \
	X(11, normals_gs, rgba16f) \

#define RAY_LIGHT_DIRECT_OUTPUTS(X) \
	X(13, light_poly_diffuse, rgba16f) \
	X(14, light_poly_specular, rgba16f) \

