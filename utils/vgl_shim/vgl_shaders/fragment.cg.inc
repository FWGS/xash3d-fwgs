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
  return c;
}

)""
