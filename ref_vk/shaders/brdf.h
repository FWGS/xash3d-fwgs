/* Creative Commons CC0 Public Domain. To the extent possible under law,
Jakub Boksansky has waived all copyright and related or neighboring rights
to Crash Course in BRDF Implementation Code Sample.
This work is published from: Germany. */

// This is a code sample accompanying the "Crash Course in BRDF Implementation" article
// v1.1, February 2021

// Xash3D amendments:
// - remove c++ compat
// - port to glsl

float rsqrt(float x) { return inversesqrt(x); }
float saturate(float x) { return clamp(x, 0.0f, 1.0f); }
vec3 saturate(vec3 x) { return clamp(x, vec3(0.0f), vec3(1.0f)); }

#define OUT_PARAMETER(X) out X

// -------------------------------------------------------------------------
//    Constant Definitions
// -------------------------------------------------------------------------

#define NONE 0

// NDF definitions
#define GGX 1
#define BECKMANN 2

// Specular BRDFs
#define MICROFACET 1
#define PHONG 2

// Diffuse BRDFs
#define LAMBERTIAN 1
#define OREN_NAYAR 2
#define DISNEY 3
#define FROSTBITE 4

// BRDF types
#define DIFFUSE_TYPE 1
#define SPECULAR_TYPE 2

// PIs
#ifndef PI
#define PI 3.141592653589f
#endif

#ifndef TWO_PI
#define TWO_PI (2.0f * PI)
#endif

#ifndef ONE_OVER_PI
#define ONE_OVER_PI (1.0f / PI)
#endif

#ifndef ONE_OVER_TWO_PI
#define ONE_OVER_TWO_PI (1.0f / TWO_PI)
#endif

// -------------------------------------------------------------------------
//    Configuration macros (user editable - set your preferences here)
// -------------------------------------------------------------------------

// Specify what NDF (GGX or BECKMANN you want to use)
#ifndef MICROFACET_DISTRIBUTION
#define MICROFACET_DISTRIBUTION GGX
//#define MICROFACET_DISTRIBUTION BECKMANN
#endif

// Specify default specular and diffuse BRDFs
#ifndef SPECULAR_BRDF
#define SPECULAR_BRDF MICROFACET
//#define SPECULAR_BRDF PHONG
//#define SPECULAR_BRDF NONE
#endif

#ifndef DIFFUSE_BRDF
#define DIFFUSE_BRDF LAMBERTIAN
//#define DIFFUSE_BRDF OREN_NAYAR
//#define DIFFUSE_BRDF DISNEY
//#define DIFFUSE_BRDF FROSTBITE
//#define DIFFUSE_BRDF NONE
#endif

// Specifies minimal reflectance for dielectrics (when metalness is zero)
// Nothing has lower reflectance than 2%, but we use 4% to have consistent results with UE4, Frostbite, et al.
#define MIN_DIELECTRICS_F0 0.04f

// Enable this to weigh diffuse by Fresnel too, otherwise specular and diffuse will be simply added together
// (this is disabled by default for Frostbite diffuse which is normalized to combine well with GGX Specular BRDF)
#if DIFFUSE_BRDF != FROSTBITE
#define COMBINE_BRDFS_WITH_FRESNEL 1
#endif

// Uncomment this to use "general" version of G1 which is not optimized and uses NDF-specific G_Lambda (can be useful for experimenting and debugging)
//#define Smith_G1 Smith_G1_General

// Enable optimized G2 implementation which includes division by specular BRDF denominator (not available for all NDFs, check macro G2_DIVIDED_BY_DENOMINATOR if it was actually used)
#define USE_OPTIMIZED_G2 1

// Enable height correlated version of G2 term. Separable version will be used otherwise
#define USE_HEIGHT_CORRELATED_G2 1

// -------------------------------------------------------------------------
//    Automatically resolved macros based on preferences (don't edit these)
// -------------------------------------------------------------------------

// Select distribution function
#if MICROFACET_DISTRIBUTION == GGX
#define Microfacet_D GGX_D
#elif MICROFACET_DISTRIBUTION == BECKMANN
#define Microfacet_D Beckmann_D
#endif

// Select G functions (masking/shadowing) depending on selected distribution
#if MICROFACET_DISTRIBUTION == GGX
#define Smith_G_Lambda Smith_G_Lambda_GGX
#elif MICROFACET_DISTRIBUTION == BECKMANN
#define Smith_G_Lambda Smith_G_Lambda_Beckmann_Walter
#endif

#ifndef Smith_G1
// Define version of G1 optimized specifically for selected NDF
#if MICROFACET_DISTRIBUTION == GGX
#define Smith_G1 Smith_G1_GGX
#elif MICROFACET_DISTRIBUTION == BECKMANN
#define Smith_G1 Smith_G1_Beckmann_Walter
#endif
#endif

// Select default specular and diffuse BRDF functions
#if SPECULAR_BRDF == MICROFACET
#define evalSpecular evalMicrofacet
#define sampleSpecular sampleSpecularMicrofacet
#if MICROFACET_DISTRIBUTION == GGX
#define sampleSpecularHalfVector sampleGGXVNDF
#else
#define sampleSpecularHalfVector sampleBeckmannWalter
#endif
#elif SPECULAR_BRDF == PHONG
#define evalSpecular evalPhong
#define sampleSpecular sampleSpecularPhong
#define sampleSpecularHalfVector samplePhong
#else
#define evalSpecular evalVoid
#define sampleSpecular sampleSpecularVoid
#define sampleSpecularHalfVector sampleSpecularHalfVectorVoid
#endif

#if MICROFACET_DISTRIBUTION == GGX
#define specularSampleWeight specularSampleWeightGGXVNDF
#define specularPdf sampleGGXVNDFReflectionPdf
#else
#define specularSampleWeight specularSampleWeightBeckmannWalter
#define specularPdf sampleBeckmannWalterReflectionPdf
#endif

#if DIFFUSE_BRDF == LAMBERTIAN
#define evalDiffuse evalLambertian
#define diffuseTerm lambertian
#elif DIFFUSE_BRDF == OREN_NAYAR
#define evalDiffuse evalOrenNayar
#define diffuseTerm orenNayar
#elif DIFFUSE_BRDF == DISNEY
#define evalDiffuse evalDisneyDiffuse
#define diffuseTerm disneyDiffuse
#elif DIFFUSE_BRDF == FROSTBITE
#define evalDiffuse evalFrostbiteDisneyDiffuse
#define diffuseTerm frostbiteDisneyDiffuse
#else
#define evalDiffuse evalVoid
#define evalIndirectDiffuse evalIndirectVoid
#define diffuseTerm none
#endif

// -------------------------------------------------------------------------
//    Structures
// -------------------------------------------------------------------------

struct MaterialProperties
{
	vec3 baseColor;
	float metalness;

	vec3 emissive;
	float roughness;

	//float transmissivness;
	//float opacity;
};

// Data needed to evaluate BRDF (surface and material properties at given point + configuration of light and normal vectors)
struct BrdfData
{
	// Material properties
	vec3 specularF0;
	vec3 diffuseReflectance;

	// Roughnesses
	float roughness;    //< perceptively linear roughness (artist's input)
	float alpha;        //< linear roughness - often 'alpha' in specular BRDF equations
	float alphaSquared; //< alpha squared - pre-calculated value commonly used in BRDF equations

	// Commonly used terms for BRDF evaluation
	vec3 F; //< Fresnel term

	// Vectors
	vec3 V; //< Direction to viewer (or opposite direction of incident ray)
	vec3 N; //< Shading normal
	vec3 H; //< Half vector (microfacet normal)
	vec3 L; //< Direction to light (or direction of reflecting ray)

	float NdotL;
	float NdotV;

	float LdotH;
	float NdotH;
	float VdotH;

	// True when V/L is backfacing wrt. shading normal N
	bool Vbackfacing;
	bool Lbackfacing;
};

// -------------------------------------------------------------------------
//    Utilities
// -------------------------------------------------------------------------

// Converts Phong's exponent (shininess) to Beckmann roughness (alpha)
// Source: "Microfacet Models for Refraction through Rough Surfaces" by Walter et al.
float shininessToBeckmannAlpha(float shininess) {
	return sqrt(2.0f / (shininess + 2.0f));
}

// Converts Beckmann roughness (alpha) to Phong's exponent (shininess)
// Source: "Microfacet Models for Refraction through Rough Surfaces" by Walter et al.
float beckmannAlphaToShininess(float alpha) {
	return 2.0f / min(0.9999f, max(0.0002f, (alpha * alpha))) - 2.0f;
}

// Converts Beckmann roughness (alpha) to Oren-Nayar roughness (sigma)
// Source: "Moving Frostbite to Physically Based Rendering" by Lagarde & de Rousiers
float beckmannAlphaToOrenNayarRoughness(float alpha) {
	return 0.7071067f * atan(alpha);
}

float luminance(vec3 rgb)
{
	return dot(rgb, vec3(0.2126f, 0.7152f, 0.0722f));
}

vec3 baseColorToSpecularF0(vec3 baseColor, float metalness) {
	return mix(vec3(MIN_DIELECTRICS_F0, MIN_DIELECTRICS_F0, MIN_DIELECTRICS_F0), baseColor, metalness);
}

vec3 baseColorToDiffuseReflectance(vec3 baseColor, float metalness)
{
	return baseColor * (1.0f - metalness);
}

float none(const BrdfData data) {
	return 0.0f;
}

vec3 evalVoid(const BrdfData data) {
	return vec3(0.0f, 0.0f, 0.0f);
}

void evalIndirectVoid(const BrdfData data, vec2 u, OUT_PARAMETER(vec3) rayDirection, OUT_PARAMETER(vec3) weight) {
	rayDirection = vec3(0.0f, 0.0f, 1.0f);
	weight = vec3(0.0f, 0.0f, 0.0f);
}

vec3 sampleSpecularVoid(vec3 Vlocal, float alpha, float alphaSquared, vec3 specularF0, vec2 u, OUT_PARAMETER(vec3) weight) {
	weight = vec3(0.0f, 0.0f, 0.0f);
	return vec3(0.0f, 0.0f, 0.0f);
}

vec3 sampleSpecularHalfVectorVoid(vec3 Vlocal, vec2 alpha2D, vec2 u) {
	return vec3(0.0f, 0.0f, 0.0f);
}

// -------------------------------------------------------------------------
//    Quaternion rotations
// -------------------------------------------------------------------------

// Calculates rotation quaternion from input vector to the vector (0, 0, 1)
// Input vector must be normalized!
vec4 getRotationToZAxis(vec3 v) {

	// Handle special case when input is exact or near opposite of (0, 0, 1)
	if (v.z < -0.99999f) return vec4(1.0f, 0.0f, 0.0f, 0.0f);

	return normalize(vec4(v.y, -v.x, 0.0f, 1.0f + v.z));
}

// Calculates rotation quaternion from vector (0, 0, 1) to the input vector
// Input vector must be normalized!
vec4 getRotationFromZAxis(vec3 v) {

	// Handle special case when input is exact or near opposite of (0, 0, 1)
	if (v.z < -0.99999f) return vec4(1.0f, 0.0f, 0.0f, 0.0f);

	return normalize(vec4(-v.y, v.x, 0.0f, 1.0f + v.z));
}

// Returns the quaternion with inverted rotation
vec4 invertRotation(vec4 q)
{
	return vec4(-q.x, -q.y, -q.z, q.w);
}

// Optimized point rotation using quaternion
// Source: https://gamedev.stackexchange.com/questions/28395/rotating-vector3-by-a-quaternion
vec3 rotatePoint(vec4 q, vec3 v) {
	const vec3 qAxis = vec3(q.x, q.y, q.z);
	return 2.0f * dot(qAxis, v) * qAxis + (q.w * q.w - dot(qAxis, qAxis)) * v + 2.0f * q.w * cross(qAxis, v);
}

// -------------------------------------------------------------------------
//    Sampling
// -------------------------------------------------------------------------

// Samples a direction within a hemisphere oriented along +Z axis with a cosine-weighted distribution
// Source: "Sampling Transformations Zoo" in Ray Tracing Gems by Shirley et al.
vec3 sampleHemisphere(vec2 u, OUT_PARAMETER(float) pdf) {

	float a = sqrt(u.x);
	float b = TWO_PI * u.y;

	vec3 result = vec3(
		a * cos(b),
		a * sin(b),
		sqrt(1.0f - u.x));

	pdf = result.z * ONE_OVER_PI;

	return result;
}

vec3 sampleHemisphere(vec2 u) {
	float pdf;
	return sampleHemisphere(u, pdf);
}

// For sampling of all our diffuse BRDFs we use cosine-weighted hemisphere sampling, with PDF equal to (NdotL/PI)
float diffusePdf(float NdotL) {
	return NdotL * ONE_OVER_PI;
}

// -------------------------------------------------------------------------
//    Fresnel
// -------------------------------------------------------------------------

// Schlick's approximation to Fresnel term
// f90 should be 1.0, except for the trick used by Schuler (see 'shadowedF90' function)
vec3 evalFresnelSchlick(vec3 f0, float f90, float NdotS)
{
	return f0 + (f90 - f0) * pow(1.0f - NdotS, 5.0f);
}

// Schlick's approximation to Fresnel term calculated using spherical gaussian approximation
// Source: https://seblagarde.wordpress.com/2012/06/03/spherical-gaussien-approximation-for-blinn-phong-phong-and-fresnel/ by Lagarde
vec3 evalFresnelSchlickSphericalGaussian(vec3 f0, float f90, float NdotV)
{
	return f0 + (f90 - f0) * exp2((-5.55473f * NdotV - 6.983146f) * NdotV);
}

// Schlick's approximation to Fresnel term with Hoffman's improvement using the Lazanyi's error term
// Source: "Fresnel Equations Considered Harmful" by Hoffman
// Also see slides http://renderwonk.com/publications/mam2019/naty_mam2019.pdf for examples and explanation of f82 term
vec3 evalFresnelHoffman(vec3 f0, float f82, float f90, float NdotS)
{
	const float alpha = 6.0f; //< Fixed to 6 in order to put peak angle for Lazanyi's error term at 82 degrees (f82)
	vec3 a = 17.6513846f * (f0 - f82) + 8.166666f * (vec3(1.0f, 1.0f, 1.0f) - f0);
	return saturate(f0 + (f90 - f0) * pow(1.0f - NdotS, 5.0f) - a * NdotS * pow(1.0f - NdotS, alpha));
}

vec3 evalFresnel(vec3 f0, float f90, float NdotS)
{
	// Default is Schlick's approximation
	return evalFresnelSchlick(f0, f90, NdotS);
}

// Attenuates F90 for very low F0 values
// Source: "An efficient and Physically Plausible Real-Time Shading Model" in ShaderX7 by Schuler
// Also see section "Overbright highlights" in Hoffman's 2010 "Crafting Physically Motivated Shading Models for Game Development" for discussion
// IMPORTANT: Note that when F0 is calculated using metalness, it's value is never less than MIN_DIELECTRICS_F0, and therefore,
// this adjustment has no effect. To be effective, F0 must be authored separately, or calculated in different way. See main text for discussion.
float shadowedF90(vec3 F0) {
	// This scaler value is somewhat arbitrary, Schuler used 60 in his article. In here, we derive it from MIN_DIELECTRICS_F0 so
	// that it takes effect for any reflectance lower than least reflective dielectrics
	//const float t = 60.0f;
	const float t = (1.0f / MIN_DIELECTRICS_F0);
	return min(1.0f, t * luminance(F0));
}

// -------------------------------------------------------------------------
//    Lambert
// -------------------------------------------------------------------------

float lambertian(const BrdfData data) {
	return 1.0f;
}

vec3 evalLambertian(const BrdfData data) {
	return data.diffuseReflectance * (ONE_OVER_PI * data.NdotL);
}

// -------------------------------------------------------------------------
//    Phong
// -------------------------------------------------------------------------

// For derivation see "Phong Normalization Factor derivation" by Giesen
float phongNormalizationTerm(float shininess) {

	return (1.0f + shininess) * ONE_OVER_TWO_PI;
}

vec3 evalPhong(const BrdfData data) {

	// First convert roughness to shininess (Phong exponent)
	float shininess = beckmannAlphaToShininess(data.alpha);

	vec3 R = reflect(-data.L, data.N);
	return data.specularF0 * (phongNormalizationTerm(shininess) * pow(max(0.0f, dot(R, data.V)), shininess) * data.NdotL);
}

// Samples a Phong distribution lobe oriented along +Z axis
// Source: "Sampling Transformations Zoo" in Ray Tracing Gems by Shirley et al.
vec3 samplePhong(vec3 Vlocal, float shininess, vec2 u, OUT_PARAMETER(float) pdf) {

	float cosTheta = pow(1.0f - u.x, 1.0f / (1.0f + shininess));
	float sinTheta = sqrt(1.0f - cosTheta * cosTheta);

	float phi = TWO_PI * u.y;

	pdf = phongNormalizationTerm(shininess) * pow(cosTheta, shininess);

	return vec3(
		cos(phi) * sinTheta,
		sin(phi) * sinTheta,
		cosTheta);
}

vec3 samplePhong(vec3 Vlocal, vec2 alpha2D, vec2 u) {
	float shininess = beckmannAlphaToShininess(dot(alpha2D, vec2(0.5f, 0.5f)));
	float pdf;
	return samplePhong(Vlocal, shininess, u, pdf);
}

// Sampling the specular BRDF based on Phong, includes normalization term
vec3 sampleSpecularPhong(vec3 Vlocal, float alpha, float alphaSquared, vec3 specularF0, vec2 u, OUT_PARAMETER(vec3) weight) {

	// First convert roughness to shininess (Phong exponent)
	float shininess = beckmannAlphaToShininess(alpha);

	float pdf;
	vec3 LPhong = samplePhong(Vlocal, shininess, u, pdf);

	// Sampled LPhong is in "lobe space" - where Phong lobe is centered around +Z axis
	// We need to rotate it in direction of perfect reflection
	vec3 Nlocal = vec3(0.0f, 0.0f, 1.0f);
	vec3 lobeDirection = reflect(-Vlocal, Nlocal);
	vec3 Llocal = rotatePoint(getRotationFromZAxis(lobeDirection), LPhong);

	// Calculate the weight of the sample
	vec3 Rlocal = reflect(-Llocal, Nlocal);
	float NdotL = max(0.00001f, dot(Nlocal, Llocal));
	weight = max(vec3(0.0f, 0.0f, 0.0f), specularF0 * NdotL);

	// Unoptimized formula was:
	//weight = specularF0 * (phongNormalizationTerm(shininess) * pow(max(0.0f, dot(Rlocal, Vlocal)), shininess) * NdotL) / pdf;

	return Llocal;
}

// -------------------------------------------------------------------------
//    Oren-Nayar
// -------------------------------------------------------------------------

// Based on Oren-Nayar's qualitative model
// Source: "Generalization of Lambert's Reflectance Model" by Oren & Nayar
float orenNayar(BrdfData data) {

	// Oren-Nayar roughness (sigma) is in radians - use conversion from Beckmann roughness here
	float sigma = beckmannAlphaToOrenNayarRoughness(data.alpha);

	float thetaV = acos(data.NdotV);
	float thetaL = acos(data.NdotL);

	float alpha = max(thetaV, thetaL);
	float beta = min(thetaV, thetaL);

	// Calculate cosine of azimuth angles difference - by projecting L and V onto plane defined by N. Assume L, V, N are normalized.
	vec3 l = data.L - data.NdotL * data.N;
	vec3 v = data.V - data.NdotV * data.N;
	float cosPhiDifference = dot(normalize(v), normalize(l));

	float sigma2 = sigma * sigma;
	float A = 1.0f - 0.5f * (sigma2 / (sigma2 + 0.33f));
	float B = 0.45f * (sigma2 / (sigma2 + 0.09f));

	return (A + B * max(0.0f, cosPhiDifference) * sin(alpha) * tan(beta));
}

vec3 evalOrenNayar(const BrdfData data) {
	return data.diffuseReflectance * (orenNayar(data) * ONE_OVER_PI * data.NdotL);
}

// -------------------------------------------------------------------------
//    Disney
// -------------------------------------------------------------------------

// Disney's diffuse term
// Source "Physically-Based Shading at Disney" by Burley
float disneyDiffuse(const BrdfData data) {

	float FD90MinusOne = 2.0f * data.roughness * data.LdotH * data.LdotH - 0.5f;

	float FDL = 1.0f + (FD90MinusOne * pow(1.0f - data.NdotL, 5.0f));
	float FDV = 1.0F + (FD90MinusOne * pow(1.0f - data.NdotV, 5.0f));

	return FDL * FDV;
}

vec3 evalDisneyDiffuse(const BrdfData data) {
	return data.diffuseReflectance * (disneyDiffuse(data) * ONE_OVER_PI * data.NdotL);
}

// Frostbite's version of Disney diffuse with energy normalization.
// Source: "Moving Frostbite to Physically Based Rendering" by Lagarde & de Rousiers
float frostbiteDisneyDiffuse(const BrdfData data) {
	float energyBias = 0.5f * data.roughness;
	float energyFactor = mix(1.0f, 1.0f / 1.51f, data.roughness);

	float FD90MinusOne = energyBias + 2.0 * data.LdotH * data.LdotH * data.roughness - 1.0f;

	float FDL = 1.0f + (FD90MinusOne * pow(1.0f - data.NdotL, 5.0f));
	float FDV = 1.0f + (FD90MinusOne * pow(1.0f - data.NdotV, 5.0f));

	return FDL * FDV * energyFactor;
}

vec3 evalFrostbiteDisneyDiffuse(const BrdfData data) {
	return data.diffuseReflectance * (frostbiteDisneyDiffuse(data) * ONE_OVER_PI * data.NdotL);
}

// -------------------------------------------------------------------------
//    Smith G term
// -------------------------------------------------------------------------

// Function to calculate 'a' parameter for lambda functions needed in Smith G term
// This is a version for shape invariant (isotropic) NDFs
// Note: makse sure NdotS is not negative
float Smith_G_a(float alpha, float NdotS) {
	return NdotS / (max(0.00001f, alpha) * sqrt(1.0f - min(0.99999f, NdotS * NdotS)));
}

// Lambda function for Smith G term derived for GGX distribution
float Smith_G_Lambda_GGX(float a) {
	return (-1.0f + sqrt(1.0f + (1.0f / (a * a)))) * 0.5f;
}

// Lambda function for Smith G term derived for Beckmann distribution
// This is Walter's rational approximation (avoids evaluating of error function)
// Source: "Real-time Rendering", 4th edition, p.339 by Akenine-Moller et al.
// Note that this formulation is slightly optimized and different from Walter's
float Smith_G_Lambda_Beckmann_Walter(float a) {
	if (a < 1.6f) {
		return (1.0f - (1.259f - 0.396f * a) * a) / ((3.535f + 2.181f * a) * a);
		//return ((1.0f + (2.276f + 2.577f * a) * a) / ((3.535f + 2.181f * a) * a)) - 1.0f; //< Walter's original
	} else {
		return 0.0f;
	}
}

// Smith G1 term (masking function)
// This non-optimized version uses NDF specific lambda function (G_Lambda) resolved bia macro based on selected NDF
float Smith_G1_General(float a) {
	return 1.0f / (1.0f + Smith_G_Lambda(a));
}

// Smith G1 term (masking function) optimized for GGX distribution (by substituting G_Lambda_GGX into G1)
float Smith_G1_GGX(float a) {
	float a2 = a * a;
	return 2.0f / (sqrt((a2 + 1.0f) / a2) + 1.0f);
}

// Smith G1 term (masking function) further optimized for GGX distribution (by substituting G_a into G1_GGX)
float Smith_G1_GGX(float alpha, float NdotS, float alphaSquared, float NdotSSquared) {
	return 2.0f / (sqrt(((alphaSquared * (1.0f - NdotSSquared)) + NdotSSquared) / NdotSSquared) + 1.0f);
}

// Smith G1 term (masking function) optimized for Beckmann distribution (by substituting G_Lambda_Beckmann_Walter into G1)
// Source: "Microfacet Models for Refraction through Rough Surfaces" by Walter et al.
float Smith_G1_Beckmann_Walter(float a) {
	if (a < 1.6f) {
		return ((3.535f + 2.181f * a) * a) / (1.0f + (2.276f + 2.577f * a) * a);
	} else {
		return 1.0f;
	}
}

float Smith_G1_Beckmann_Walter(float alpha, float NdotS, float alphaSquared, float NdotSSquared) {
	return Smith_G1_Beckmann_Walter(Smith_G_a(alpha, NdotS));
}

// Smith G2 term (masking-shadowing function)
// Separable version assuming independent (uncorrelated) masking and shadowing, uses G1 functions for selected NDF
float Smith_G2_Separable(float alpha, float NdotL, float NdotV) {
	float aL = Smith_G_a(alpha, NdotL);
	float aV = Smith_G_a(alpha, NdotV);
	return Smith_G1(aL) * Smith_G1(aV);
}

// Smith G2 term (masking-shadowing function)
// Height correlated version - non-optimized, uses G_Lambda functions for selected NDF
float Smith_G2_Height_Correlated(float alpha, float NdotL, float NdotV) {
	float aL = Smith_G_a(alpha, NdotL);
	float aV = Smith_G_a(alpha, NdotV);
	return 1.0f / (1.0f + Smith_G_Lambda(aL) + Smith_G_Lambda(aV));
}

// Smith G2 term (masking-shadowing function) for GGX distribution
// Separable version assuming independent (uncorrelated) masking and shadowing - optimized by substituing G_Lambda for G_Lambda_GGX and
// dividing by (4 * NdotL * NdotV) to cancel out these terms in specular BRDF denominator
// Source: "Moving Frostbite to Physically Based Rendering" by Lagarde & de Rousiers
// Note that returned value is G2 / (4 * NdotL * NdotV) and therefore includes division by specular BRDF denominator
float Smith_G2_Separable_GGX_Lagarde(float alphaSquared, float NdotL, float NdotV) {
	float a = NdotV + sqrt(alphaSquared + NdotV * (NdotV - alphaSquared * NdotV));
	float b = NdotL + sqrt(alphaSquared + NdotL * (NdotL - alphaSquared * NdotL));
	return 1.0f / (a * b);
}

// Smith G2 term (masking-shadowing function) for GGX distribution
// Height correlated version - optimized by substituing G_Lambda for G_Lambda_GGX and dividing by (4 * NdotL * NdotV) to cancel out
// the terms in specular BRDF denominator
// Source: "Moving Frostbite to Physically Based Rendering" by Lagarde & de Rousiers
// Note that returned value is G2 / (4 * NdotL * NdotV) and therefore includes division by specular BRDF denominator
float Smith_G2_Height_Correlated_GGX_Lagarde(float alphaSquared, float NdotL, float NdotV) {
	float a = NdotV * sqrt(alphaSquared + NdotL * (NdotL - alphaSquared * NdotL));
	float b = NdotL * sqrt(alphaSquared + NdotV * (NdotV - alphaSquared * NdotV));
	return 0.5f / (a + b);
}

// Smith G2 term (masking-shadowing function) for GGX distribution
// Height correlated version - approximation by Hammon
// Source: "PBR Diffuse Lighting for GGX + Smith Microsurfaces", slide 84 by Hammon
// Note that returned value is G2 / (4 * NdotL * NdotV) and therefore includes division by specular BRDF denominator
float Smith_G2_Height_Correlated_GGX_Hammon(float alpha, float NdotL, float NdotV) {
	return 0.5f / (mix(2.0f * NdotL * NdotV, NdotL + NdotV, alpha));
}

// A fraction G2/G1 where G2 is height correlated can be expressed using only G1 terms
// Source: "Implementing a Simple Anisotropic Rough Diffuse Material with Stochastic Evaluation", Appendix A by Heitz & Dupuy
float Smith_G2_Over_G1_Height_Correlated(float alpha, float alphaSquared, float NdotL, float NdotV) {
	float G1V = Smith_G1(alpha, NdotV, alphaSquared, NdotV * NdotV);
	float G1L = Smith_G1(alpha, NdotL, alphaSquared, NdotL * NdotL);
	return G1L / (G1V + G1L - G1V * G1L);
}

// Evaluates G2 for selected configuration (GGX/Beckmann, optimized/non-optimized, separable/height-correlated)
// Note that some paths aren't optimized too much...
// Also note that when USE_OPTIMIZED_G2 is specified, returned value will be: G2 / (4 * NdotL * NdotV) if GG-X is selected
float Smith_G2(float alpha, float alphaSquared, float NdotL, float NdotV) {

#if USE_OPTIMIZED_G2 && (MICROFACET_DISTRIBUTION == GGX)
#if USE_HEIGHT_CORRELATED_G2
#define G2_DIVIDED_BY_DENOMINATOR 1
	return Smith_G2_Height_Correlated_GGX_Lagarde(alphaSquared, NdotL, NdotV);
#else
#define G2_DIVIDED_BY_DENOMINATOR 1
	return Smith_G2_Separable_GGX_Lagarde(alphaSquared, NdotL, NdotV);
#endif
#else
#if USE_HEIGHT_CORRELATED_G2
	return Smith_G2_Height_Correlated(alpha, NdotL, NdotV);
#else
	return Smith_G2_Separable(alpha, NdotL, NdotV);
#endif
#endif

}

// -------------------------------------------------------------------------
//    Normal distribution functions
// -------------------------------------------------------------------------

float Beckmann_D(float alphaSquared, float NdotH)
{
	float cos2Theta = NdotH * NdotH;
	float numerator = exp((cos2Theta - 1.0f) / (alphaSquared * cos2Theta));
	float denominator = PI * alphaSquared * cos2Theta * cos2Theta;
	return numerator / denominator;
}

float GGX_D(float alphaSquared, float NdotH) {
	float b = ((alphaSquared - 1.0f) * NdotH * NdotH + 1.0f);
	return alphaSquared / (PI * b * b);
}

// -------------------------------------------------------------------------
//    Microfacet model
// -------------------------------------------------------------------------

// Samples a microfacet normal for the GGX distribution using VNDF method.
// Source: "Sampling the GGX Distribution of Visible Normals" by Heitz
// See also https://hal.inria.fr/hal-00996995v1/document and http://jcgt.org/published/0007/04/01/
// Random variables 'u' must be in <0;1) interval
// PDF is 'G1(NdotV) * D'
vec3 sampleGGXVNDF(vec3 Ve, vec2 alpha2D, vec2 u) {

	// Section 3.2: transforming the view direction to the hemisphere configuration
	vec3 Vh = normalize(vec3(alpha2D.x * Ve.x, alpha2D.y * Ve.y, Ve.z));

	// Section 4.1: orthonormal basis (with special case if cross product is zero)
	float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
	vec3 T1 = lensq > 0.0f ? vec3(-Vh.y, Vh.x, 0.0f) * rsqrt(lensq) : vec3(1.0f, 0.0f, 0.0f);
	vec3 T2 = cross(Vh, T1);

	// Section 4.2: parameterization of the projected area
	float r = sqrt(u.x);
	float phi = TWO_PI * u.y;
	float t1 = r * cos(phi);
	float t2 = r * sin(phi);
	float s = 0.5f * (1.0f + Vh.z);
	t2 = mix(sqrt(1.0f - t1 * t1), t2, s);

	// Section 4.3: reprojection onto hemisphere
	vec3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0f, 1.0f - t1 * t1 - t2 * t2)) * Vh;

	// Section 3.4: transforming the normal back to the ellipsoid configuration
	return normalize(vec3(alpha2D.x * Nh.x, alpha2D.y * Nh.y, max(0.0f, Nh.z)));
}

// PDF of sampling a reflection vector L using 'sampleGGXVNDF'.
// Note that PDF of sampling given microfacet normal is (G1 * D) when vectors are in local space (in the hemisphere around shading normal).
// Remaining terms (1.0f / (4.0f * NdotV)) are specific for reflection case, and come from multiplying PDF by jacobian of reflection operator
float sampleGGXVNDFReflectionPdf(float alpha, float alphaSquared, float NdotH, float NdotV, float LdotH) {
	NdotH = max(0.00001f, NdotH);
	NdotV = max(0.00001f, NdotV);
	return (GGX_D(max(0.00001f, alphaSquared), NdotH) * Smith_G1_GGX(alpha, NdotV, alphaSquared, NdotV * NdotV)) / (4.0f * NdotV);
}

// "Walter's trick" is an adjustment of alpha value for Walter's sampling to reduce maximal weight of sample to about 4
// Source: "Microfacet Models for Refraction through Rough Surfaces" by Walter et al., page 8
float waltersTrick(float alpha, float NdotV) {
	return (1.2f - 0.2f * sqrt(abs(NdotV))) * alpha;
}

// PDF of sampling a reflection vector L using 'sampleBeckmannWalter'.
// Note that PDF of sampling given microfacet normal is (D * NdotH). Remaining terms (1.0f / (4.0f * LdotH)) are specific for
// reflection case, and come from multiplying PDF by jacobian of reflection operator
float sampleBeckmannWalterReflectionPdf(float alpha, float alphaSquared, float NdotH, float NdotV, float LdotH) {
	NdotH = max(0.00001f, NdotH);
	LdotH = max(0.00001f, LdotH);
	return Beckmann_D(max(0.00001f, alphaSquared), NdotH) * NdotH / (4.0f * LdotH);
}

// Samples a microfacet normal for the Beckmann distribution using walter's method.
// Source: "Microfacet Models for Refraction through Rough Surfaces" by Walter et al.
// PDF is 'D * NdotH'
vec3 sampleBeckmannWalter(vec3 Vlocal, vec2 alpha2D, vec2 u) {
	float alpha = dot(alpha2D, vec2(0.5f, 0.5f));

	// Equations (28) and (29) from Walter's paper for Beckmann distribution
	float tanThetaSquared = -(alpha * alpha) * log(1.0f - u.x);
	float phi = TWO_PI * u.y;

	// Calculate cosTheta and sinTheta needed for conversion to H vector
	float cosTheta = rsqrt(1.0f + tanThetaSquared);
	float sinTheta = sqrt(1.0f - cosTheta * cosTheta);

	// Convert sampled spherical coordinates to H vector
	return normalize(vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta));
}

// Weight for the reflection ray sampled from GGX distribution using VNDF method
float specularSampleWeightGGXVNDF(float alpha, float alphaSquared, float NdotL, float NdotV, float HdotL, float NdotH) {
#if USE_HEIGHT_CORRELATED_G2
	return Smith_G2_Over_G1_Height_Correlated(alpha, alphaSquared, NdotL, NdotV);
#else
	return Smith_G1_GGX(alpha, NdotL, alphaSquared, NdotL * NdotL);
#endif
}

// Weight for the reflection ray sampled from Beckmann distribution using Walter's method
float specularSampleWeightBeckmannWalter(float alpha, float alphaSquared, float NdotL, float NdotV, float HdotL, float NdotH) {
	return (HdotL * Smith_G2(alpha, alphaSquared, NdotL, NdotV)) / (NdotV * NdotH);
}

// Samples a reflection ray from the rough surface using selected microfacet distribution and sampling method
// Resulting weight includes multiplication by cosine (NdotL) term
vec3 sampleSpecularMicrofacet(vec3 Vlocal, float alpha, float alphaSquared, vec3 specularF0, vec2 u, OUT_PARAMETER(vec3) weight) {

	// Sample a microfacet normal (H) in local space
	vec3 Hlocal;
	if (alpha == 0.0f) {
		// Fast path for zero roughness (perfect reflection), also prevents NaNs appearing due to divisions by zeroes
		Hlocal = vec3(0.0f, 0.0f, 1.0f);
	} else {
		// For non-zero roughness, this calls VNDF sampling for GG-X distribution or Walter's sampling for Beckmann distribution
		Hlocal = sampleSpecularHalfVector(Vlocal, vec2(alpha, alpha), u);
	}

	// Reflect view direction to obtain light vector
	vec3 Llocal = reflect(-Vlocal, Hlocal);

	// Note: HdotL is same as HdotV here
	// Clamp dot products here to small value to prevent numerical instability. Assume that rays incident from below the hemisphere have been filtered
	float HdotL = max(0.00001f, min(1.0f, dot(Hlocal, Llocal)));
	const vec3 Nlocal = vec3(0.0f, 0.0f, 1.0f);
	float NdotL = max(0.00001f, min(1.0f, dot(Nlocal, Llocal)));
	float NdotV = max(0.00001f, min(1.0f, dot(Nlocal, Vlocal)));
	float NdotH = max(0.00001f, min(1.0f, dot(Nlocal, Hlocal)));
	vec3 F = evalFresnel(specularF0, shadowedF90(specularF0), HdotL);

	// Calculate weight of the sample specific for selected sampling method
	// (this is microfacet BRDF divided by PDF of sampling method - notice how most terms cancel out)
	weight = F * specularSampleWeight(alpha, alphaSquared, NdotL, NdotV, HdotL, NdotH);

	return Llocal;
}

// Evaluates microfacet specular BRDF
vec3 evalMicrofacet(const BrdfData data) {

	float D = Microfacet_D(max(0.00001f, data.alphaSquared), data.NdotH);
	float G2 = Smith_G2(data.alpha, data.alphaSquared, data.NdotL, data.NdotV);
	//vec3 F = evalFresnel(data.specularF0, shadowedF90(data.specularF0), data.VdotH); //< Unused, F is precomputed already

#if G2_DIVIDED_BY_DENOMINATOR
	return data.F * (G2 * D * data.NdotL);
#else
	return ((data.F * G2 * D) / (4.0f * data.NdotL * data.NdotV)) * data.NdotL;
#endif
}

// -------------------------------------------------------------------------
//    Combined BRDF
// -------------------------------------------------------------------------

// Precalculates commonly used terms in BRDF evaluation
// Clamps around dot products prevent NaNs and ensure numerical stability, but make sure to
// correctly ignore rays outside of the sampling hemisphere, by using 'Vbackfacing' and 'Lbackfacing' flags
BrdfData prepareBRDFData(vec3 N, vec3 L, vec3 V, MaterialProperties material) {
	BrdfData data;

	// Evaluate VNHL vectors
	data.V = V;
	data.N = N;
	data.H = normalize(L + V);
	data.L = L;

	float NdotL = dot(N, L);
	float NdotV = dot(N, V);
	data.Vbackfacing = (NdotV <= 0.0f);
	data.Lbackfacing = (NdotL <= 0.0f);

	// Clamp NdotS to prevent numerical instability. Assume vectors below the hemisphere will be filtered using 'Vbackfacing' and 'Lbackfacing' flags
	data.NdotL = min(max(0.00001f, NdotL), 1.0f);
	data.NdotV = min(max(0.00001f, NdotV), 1.0f);

	data.LdotH = saturate(dot(L, data.H));
	data.NdotH = saturate(dot(N, data.H));
	data.VdotH = saturate(dot(V, data.H));

	// Unpack material properties
	data.specularF0 = baseColorToSpecularF0(material.baseColor, material.metalness);
	data.diffuseReflectance = baseColorToDiffuseReflectance(material.baseColor, material.metalness);

	// Unpack 'perceptively linear' -> 'linear' -> 'squared' roughness
	data.roughness = material.roughness;
	data.alpha = material.roughness * material.roughness;
	data.alphaSquared = data.alpha * data.alpha;

	// Pre-calculate some more BRDF terms
	data.F = evalFresnel(data.specularF0, shadowedF90(data.specularF0), data.LdotH);

	return data;
}

// This is an entry point for evaluation of all other BRDFs based on selected configuration (for direct light)
vec3 evalCombinedBRDF(vec3 N, vec3 L, vec3 V, MaterialProperties material) {

	// Prepare data needed for BRDF evaluation - unpack material properties and evaluate commonly used terms (e.g. Fresnel, NdotL, ...)
	const BrdfData data = prepareBRDFData(N, L, V, material);

	// Ignore V and L rays "below" the hemisphere
	if (data.Vbackfacing || data.Lbackfacing) return vec3(0.0f, 0.0f, 0.0f);

	// Eval specular and diffuse BRDFs
	vec3 specular = evalSpecular(data);
	vec3 diffuse = evalDiffuse(data);

	// Combine specular and diffuse layers
#if COMBINE_BRDFS_WITH_FRESNEL
	// Specular is already multiplied by F, just attenuate diffuse
	return (vec3(1.0f, 1.0f, 1.0f) - data.F) * diffuse + specular;
#else
	return diffuse + specular;
#endif
}

// This is an entry point for evaluation of all other BRDFs based on selected configuration (for indirect light)
bool evalIndirectCombinedBRDF(vec2 u, vec3 shadingNormal, vec3 geometryNormal, vec3 V, MaterialProperties material, const int brdfType, OUT_PARAMETER(vec3) rayDirection, OUT_PARAMETER(vec3) sampleWeight) {

	// Ignore incident ray coming from "below" the hemisphere
	if (dot(shadingNormal, V) <= 0.0f) return false;

	// Transform view direction into local space of our sampling routines
	// (local space is oriented so that its positive Z axis points along the shading normal)
	vec4 qRotationToZ = getRotationToZAxis(shadingNormal);
	vec3 Vlocal = rotatePoint(qRotationToZ, V);
	const vec3 Nlocal = vec3(0.0f, 0.0f, 1.0f);

	vec3 rayDirectionLocal = vec3(0.0f, 0.0f, 0.0f);

	if (brdfType == DIFFUSE_TYPE) {

		// Sample diffuse ray using cosine-weighted hemisphere sampling
		rayDirectionLocal = sampleHemisphere(u);
		const BrdfData data = prepareBRDFData(Nlocal, rayDirectionLocal, Vlocal, material);

		// Function 'diffuseTerm' is predivided by PDF of sampling the cosine weighted hemisphere
		sampleWeight = data.diffuseReflectance * diffuseTerm(data);

#if COMBINE_BRDFS_WITH_FRESNEL
		// Sample a half-vector of specular BRDF. Note that we're reusing random variable 'u' here, but correctly it should be an new independent random number
		vec3 Hspecular = sampleSpecularHalfVector(Vlocal, vec2(data.alpha, data.alpha), u);

		// Clamp HdotL to small value to prevent numerical instability. Assume that rays incident from below the hemisphere have been filtered
		float VdotH = max(0.00001f, min(1.0f, dot(Vlocal, Hspecular)));
		sampleWeight *= (vec3(1.0f, 1.0f, 1.0f) - evalFresnel(data.specularF0, shadowedF90(data.specularF0), VdotH));
#endif

	} else if (brdfType == SPECULAR_TYPE) {
		const BrdfData data = prepareBRDFData(Nlocal, vec3(0.0f, 0.0f, 1.0f) /* unused L vector */, Vlocal, material);
		rayDirectionLocal = sampleSpecular(Vlocal, data.alpha, data.alphaSquared, data.specularF0, u, sampleWeight);
	}

	// Prevent tracing direction with no contribution
	if (luminance(sampleWeight) == 0.0f) return false;

	// Transform sampled direction Llocal back to V vector space
	rayDirection = normalize(rotatePoint(invertRotation(qRotationToZ), rayDirectionLocal));

	// Prevent tracing direction "under" the hemisphere (behind the triangle)
	if (dot(geometryNormal, rayDirection) <= 0.0f) return false;

	return true;
}
