#ifndef UTILS_GLSL_INCLUDED
#define UTILS_GLSL_INCLUDED

float signP(float v) { return v >= 0.f ? 1.f : -1.f; }
vec2 signP(vec2 v) { return vec2(signP(v.x), signP(v.y)); }

// https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
// https://www.shadertoy.com/view/Mtfyzl
vec2 OctWrap( vec2 v )
{
    return ( 1.0 - abs( v.yx ) ) * signP(v.xy);
}

vec2 normalEncode( vec3 n )
{
    n /= ( abs( n.x ) + abs( n.y ) + abs( n.z ) );
    n.xy = n.z >= 0.0 ? n.xy : OctWrap( n.xy );
    n.xy = n.xy * 0.5 + 0.5;
    return n.xy;
}

vec3 normalDecode( vec2 f )
{
    f = f * 2.0 - 1.0;

    // https://twitter.com/Stubbesaurus/status/937994790553227264
    vec3 n = vec3( f, 1.0 - abs( f.x ) - abs( f.y ) );
    const float t = max( -n.z, 0.f );
    n.xy -= t * signP(n.xy);
    return normalize( n );
}

vec2 baryMix(vec2 v1, vec2 v2, vec2 v3, vec2 bary) {
	return v1 * (1. - bary.x - bary.y) + v2 * bary.x + v3 * bary.y;
}

vec3 baryMix(vec3 v1, vec3 v2, vec3 v3, vec2 bary) {
	return v1 * (1. - bary.x - bary.y) + v2 * bary.x + v3 * bary.y;
}

vec4 baryMix(vec4 v1, vec4 v2, vec4 v3, vec2 bary) {
	return v1 * (1. - bary.x - bary.y) + v2 * bary.x + v3 * bary.y;
}
#endif // UTILS_GLSL_INCLUDED
