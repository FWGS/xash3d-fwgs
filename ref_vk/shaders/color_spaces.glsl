#define SRGB_FAST_APPROXIMATION 0

#ifdef SRGB_FAST_APPROXIMATION
#define LINEARtoSRGB OECF_sRGBFast
#define SRGBtoLINEAR sRGB_OECFFast
#else
#define LINEARtoSRGB OECF_sRGB
#define SRGBtoLINEAR sRGB_OECF
#endif

// based on https://github.com/OGRECave/ogre/blob/f49bc9be79f6711a88f01892711120da717f6148/Samples/Media/PBR/filament/pbr_filament.frag.glsl#L108-L124
float sRGB_OECF(const float sRGB) {
	// IEC 61966-2-1:1999
	float linearLow = sRGB / 12.92;
	float linearHigh = pow((sRGB + 0.055) / 1.055, 2.4);
	return sRGB <= 0.04045 ? linearLow : linearHigh;
}
/**
 * Reverse opto-electronic conversion function to the one that filament
 * provides. Filament version has LDR RGB linear color -> LDR RGB non-linear
 * color in sRGB space. This function will thus provide LDR RGB non-linear
 * color in sRGB space -> LDR RGB linear color conversion.
 */
vec3 sRGB_OECF(const vec3 sRGB)
{
	return vec3(sRGB_OECF(sRGB.r), sRGB_OECF(sRGB.g), sRGB_OECF(sRGB.b));
}
vec4 sRGB_OECF(const vec4 sRGB)
{
	return vec4(sRGB_OECF(sRGB.r), sRGB_OECF(sRGB.g), sRGB_OECF(sRGB.b), sRGB.w);
}
vec3 sRGB_OECFFast(const vec3 sRGB) {
	return pow(sRGB, vec3(2.2));
}
vec4 sRGB_OECFFast(const vec4 sRGB) {
	return vec4(pow(sRGB.rgb, vec3(2.2)), sRGB.w);
}

// based on https://github.com/abhirocks1211/filament/blob/3e97ac5268a47d5625c7d166eb7dda0bbba14a4d/shaders/src/conversion_functions.fs#L20-L55
//------------------------------------------------------------------------------
// Opto-electronic conversion functions (linear to non-linear)
//------------------------------------------------------------------------------

float OECF_sRGB(const float linear) {
	// IEC 61966-2-1:1999
	float sRGBLow  = linear * 12.92;
	float sRGBHigh = (pow(linear, 1.0 / 2.4) * 1.055) - 0.055;
	return linear <= 0.0031308 ? sRGBLow : sRGBHigh;
}

vec3 OECF_sRGB(const vec3 linear) {
	return vec3(OECF_sRGB(linear.r), OECF_sRGB(linear.g), OECF_sRGB(linear.b));
}
vec4 OECF_sRGB(const vec4 linear) {
	return vec4(OECF_sRGB(linear.r), OECF_sRGB(linear.g), OECF_sRGB(linear.b), linear.w);
}
vec3 OECF_sRGBFast(const vec3 linear) {
	return pow(linear, vec3(1.0 / 2.2));
}
vec4 OECF_sRGBFast(const vec4 linear) {
	return vec4(pow(linear.rgb, vec3(1.0 / 2.2)), linear.w);
}
