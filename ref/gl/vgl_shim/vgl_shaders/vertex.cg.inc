R""(

// has to be called this for VGL to fill it in automatically
uniform float4x4 gl_ModelViewProjectionMatrix;

void main(
    float3 inPosition
#if ATTR_COLOR
  , float4 inColor
#endif
#if ATTR_NORMAL
  , float3 inNormal
#endif
#if ATTR_TEXCOORD0
  , float2 inTexCoord0
#endif
#if ATTR_TEXCOORD1
  , float2 inTexCoord1
#endif
  , float4 out vPosition : POSITION
#if ATTR_COLOR
  , float4 out vColor : COLOR
#endif
#if ATTR_TEXCOORD0
  , float2 out vTexCoord0 : TEXCOORD0
#endif
#if ATTR_TEXCOORD1
  , float2 out vTexCoord1 : TEXCOORD1
#endif
#if ATTR_NORMAL
  , float3 out vNormal : TEXCOORD2
#endif
) {
  vPosition = mul(gl_ModelViewProjectionMatrix, float4(inPosition, 1.f));
#if ATTR_COLOR
  vColor = inColor;
#endif
#if ATTR_NORMAL
  vNormal = inNormal;
#endif
#if ATTR_TEXCOORD0
  vTexCoord0 = inTexCoord0;
#endif
#if ATTR_TEXCOORD1
  vTexCoord1 = inTexCoord1;
#endif
}

)""
