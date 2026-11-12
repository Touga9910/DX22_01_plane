#include "common.hlsl"

// This input exactly matches Shader.cpp's VERTEX_3D input layout.
struct BILLIARD_VS_IN
{
    float3 pos : POSITION0;
    float3 nrm : NORMAL0;
    float4 col : COLOR0;
    float2 tex : TEXCOORD0;
};

// Same base lighting as litTextureVS.hlsl, with world geometry supplied
// to the pixel shader for reflections. No camera-facing geometry or rotation.
struct BILLIARD_PS_IN
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 tex : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float3 normalWS : TEXCOORD2;
};

BILLIARD_PS_IN main(BILLIARD_VS_IN input)
{
    BILLIARD_PS_IN output;

    matrix wvp = mul(World, View);
    wvp = mul(wvp, Projection);
    const float4 localPosition = float4(input.pos, 1.0f);
    output.pos = mul(localPosition, wvp);

    // These six lines reproduce the previous litTextureVS.hlsl lighting.
    float4 normal = float4(input.nrm, 0.0f);
    float4 worldNormal = mul(normal, World);
    worldNormal = normalize(worldNormal);
    float d = -dot(Light.Direction.xyz, worldNormal.xyz);
    d = saturate(d);
    output.col.xyz = input.col.xyz * d * Light.Diffuse.xyz;
    output.col.xyz += input.col.xyz * Light.Ambient.xyz;
    output.col.xyz += Material.Emission.xyz;
    output.col.a = input.col.a * Material.Diffuse.a;
    output.tex = input.tex;

    output.worldPos = mul(localPosition, World).xyz;
    output.normalWS = worldNormal.xyz;
    return output;
}
