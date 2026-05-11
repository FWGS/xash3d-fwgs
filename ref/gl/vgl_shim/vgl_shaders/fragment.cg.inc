R""(

#if ATTR_TEXCOORD0
uniform sampler2D uTex0 : TEXUNIT0;
#endif
#if ATTR_TEXCOORD1
uniform sampler2D uTex1 : TEXUNIT1;
#endif
#if FEAT_ALPHA_TEST
uniform float uAlphaTest;
#endif
#if FEAT_FOG
// color + density, mode always GL_EXP
uniform float4 uFog;
#endif

float4 main(
    uniform float4 uColor
#if ATTR_COLOR
  , float4 vColor : COLOR
#endif
#if ATTR_TEXCOORD0
  , float2 vTexCoord0 : TEXCOORD0
#endif
#if ATTR_TEXCOORD1
  , float2 vTexCoord1 : TEXCOORD1
#endif
#if ATTR_NORMAL
  , float3 vNormal : TEXCOORD2
#endif
#if FEAT_FOG
  , float4 vPosition : WPOS
#endif
) {
#if ATTR_COLOR
  float4 c = vColor;
#else
  float4 c = uColor;
#endif
#if ATTR_TEXCOORD0
  c *= tex2D(uTex0, vTexCoord0);
#endif
#if ATTR_TEXCOORD1
  c *= tex2D(uTex1, vTexCoord1);
#endif
#if FEAT_ALPHA_TEST
  if (c.a <= uAlphaTest)
    discard;
#endif
#if FEAT_FOG
  float fogDist = vPosition.z / vPosition.w;
  float fogRate = clamp(exp(-uFog.w * fogDist), 0.f, 1.f);
  c.rgb = lerp(uFog.rgb, c.rgb, fogRate);
#endif
  return c;
}

)""
