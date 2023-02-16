#ifndef NOISE_GLSL_INCLUDED
#define NOISE_GLSL_INCLUDED
// Copypasted from Mark Jarzynski and Marc Olano, Hash Functions for GPU Rendering, Journal of Computer Graphics Techniques (JCGT), vol. 9, no. 3, 21-38, 2020
// http://www.jcgt.org/published/0009/03/02/
// https://www.shadertoy.com/view/XlGcRh

// xxhash (https://github.com/Cyan4973/xxHash)
//   From https://www.shadertoy.com/view/Xt3cDn
uint xxhash32(uint p)
{
	const uint PRIME32_2 = 2246822519U, PRIME32_3 = 3266489917U;
	const uint PRIME32_4 = 668265263U, PRIME32_5 = 374761393U;
	uint h32 = p + PRIME32_5;
	h32 = PRIME32_4*((h32 << 17) | (h32 >> (32 - 17)));
    h32 = PRIME32_2*(h32^(h32 >> 15));
    h32 = PRIME32_3*(h32^(h32 >> 13));
    return h32^(h32 >> 16);
}

uint xxhash32(uvec2 p)
{
    const uint PRIME32_2 = 2246822519U, PRIME32_3 = 3266489917U;
	const uint PRIME32_4 = 668265263U, PRIME32_5 = 374761393U;
    uint h32 = p.y + PRIME32_5 + p.x*PRIME32_3;
    h32 = PRIME32_4*((h32 << 17) | (h32 >> (32 - 17)));
    h32 = PRIME32_2*(h32^(h32 >> 15));
    h32 = PRIME32_3*(h32^(h32 >> 13));
    return h32^(h32 >> 16);
}

uint xxhash32(uvec3 p)
{
    const uint PRIME32_2 = 2246822519U, PRIME32_3 = 3266489917U;
	const uint PRIME32_4 = 668265263U, PRIME32_5 = 374761393U;
	uint h32 =  p.z + PRIME32_5 + p.x*PRIME32_3;
	h32 = PRIME32_4*((h32 << 17) | (h32 >> (32 - 17)));
	h32 += p.y * PRIME32_3;
	h32 = PRIME32_4*((h32 << 17) | (h32 >> (32 - 17)));
    h32 = PRIME32_2*(h32^(h32 >> 15));
    h32 = PRIME32_3*(h32^(h32 >> 13));
    return h32^(h32 >> 16);
}

uint xxhash32(uvec4 p)
{
    const uint PRIME32_2 = 2246822519U, PRIME32_3 = 3266489917U;
	const uint PRIME32_4 = 668265263U, PRIME32_5 = 374761393U;
	uint h32 =  p.w + PRIME32_5 + p.x*PRIME32_3;
	h32 = PRIME32_4*((h32 << 17) | (h32 >> (32 - 17)));
	h32 += p.y * PRIME32_3;
	h32 = PRIME32_4*((h32 << 17) | (h32 >> (32 - 17)));
	h32 += p.z * PRIME32_3;
	h32 = PRIME32_4*((h32 << 17) | (h32 >> (32 - 17)));
    h32 = PRIME32_2*(h32^(h32 >> 15));
    h32 = PRIME32_3*(h32^(h32 >> 13));
    return h32^(h32 >> 16);
}

// https://www.pcg-random.org/
uint pcg(uint v)
{
	uint state = v * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

uvec2 pcg2d(uvec2 v)
{
    v = v * 1664525u + 1013904223u;

    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;

    v = v ^ (v>>16u);

    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;

    v = v ^ (v>>16u);

    return v;
}

// http://www.jcgt.org/published/0009/03/02/
uvec3 pcg3d(uvec3 v) {

    v = v * 1664525u + 1013904223u;

    v.x += v.y*v.z;
    v.y += v.z*v.x;
    v.z += v.x*v.y;

    v ^= v >> 16u;

    v.x += v.y*v.z;
    v.y += v.z*v.x;
    v.z += v.x*v.y;

    return v;
}

// http://www.jcgt.org/published/0009/03/02/
uvec3 pcg3d16(uvec3 v)
{
    v = v * 12829u + 47989u;

    v.x += v.y*v.z;
    v.y += v.z*v.x;
    v.z += v.x*v.y;

    v.x += v.y*v.z;
    v.y += v.z*v.x;
    v.z += v.x*v.y;

	v >>= 16u;

    return v;
}

// http://www.jcgt.org/published/0009/03/02/
uvec4 pcg4d(uvec4 v)
{
    v = v * 1664525u + 1013904223u;

    v.x += v.y*v.w;
    v.y += v.z*v.x;
    v.z += v.x*v.y;
    v.w += v.y*v.z;

    v ^= v >> 16u;

    v.x += v.y*v.w;
    v.y += v.z*v.x;
    v.z += v.x*v.y;
    v.w += v.y*v.z;

    return v;
}

uint rand01_state = 0;
uint rand() {
	return rand01_state = xxhash32(rand01_state);
}
uint rand_range(uint rmax) {
	return rand() % rmax;
}

float uintToFloat01(uint x) {
	return uintBitsToFloat(0x3f800000 | (x & 0x007fffff)) - 1.;
}

float rand01() {
	return uintToFloat01(rand());
}

vec3 rand3_f01(uvec3 seed) {
    uvec3 v = pcg3d(seed);
    return vec3(uintToFloat01(v.x), uintToFloat01(v.y), uintToFloat01(v.z));
}
#endif // NOISE_GLSL_INCLUDED
