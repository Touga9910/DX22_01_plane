#include "common.hlsl"

Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);

cbuffer BilliardReflectionBuffer : register(b6)
{
    float4 Eye;
    float4 Params; // x specular strength, y shininess, z studio environment, w defeat fade
}

struct BILLIARD_PS_IN
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 tex : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float3 normalWS : TEXCOORD2;
};

// Approximate fixed studio reflections, not the actual game table/nearby balls.
float3 StudioReflection(float3 directionWS)
{
    float t = saturate(directionWS.y * 0.5f + 0.5f);
    float3 ambientRoom = lerp(float3(0.040f, 0.048f, 0.058f),
                              float3(0.16f, 0.19f, 0.23f), t);
    float3 keyPanel = normalize(float3(-0.42f, 0.85f, -0.30f));
    float3 fillPanel = normalize(float3(0.67f, 0.71f, 0.22f));
    float key = pow(saturate(dot(directionWS, keyPanel)), 38.0f);
    float fill = pow(saturate(dot(directionWS, fillPanel)), 76.0f);
    return ambientRoom + float3(0.90f, 0.92f, 0.97f) * key
                       + float3(0.48f, 0.56f, 0.67f) * fill;
}

float4 main(BILLIARD_PS_IN input) : SV_Target
{
    // Exact base-color calculation from the already working litTexturePS.hlsl.
    // Renderer binds Material b4 to both stages; texture color remains the base.
    float4 color;
    if (Material.TextureEnable)
    {
        color = g_Texture.Sample(g_SamplerState, input.tex);
        color *= input.col;
        color.rgb *= Material.Diffuse.rgb;
    }
    else
    {
        color = input.col * Material.Diffuse;
    }

    // Desaturate only the lit base color. Highlights and environment
    // reflections are added afterwards, preserving the ball's glossy finish.
    // At full transition: 75% desaturation, 90% base brightness, unchanged alpha.
    float defeatFade = saturate(Params.w);
    defeatFade = defeatFade * defeatFade * (3.0f - 2.0f * defeatFade);
    float luminance = dot(color.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    float3 defeatedBase = lerp(color.rgb, luminance.xxx, 0.90f) * 0.90f;
    color.rgb = lerp(color.rgb, defeatedBase, defeatFade);

    float3 N = normalize(input.normalWS);
    float3 V = normalize(Eye.xyz - input.worldPos);
    // Light b3 is bound to both VS and PS by Renderer.
    float3 L = normalize(-Light.Direction.xyz);
    float3 H = normalize(L + V);

    // Additional white specular: does NOT multiply or darken original color.
    float highlight = pow(saturate(dot(N, H)), max(4.0f, Params.y));
    float3 specular = float3(1.0f, 1.0f, 1.0f) *
        (Params.x * highlight) * saturate(Material.Specular.rgb);

    // Additional environment reflection with a small normal-incidence Fresnel.
    float ndv = saturate(dot(N, V));
    float edgeFresnel = pow(1.0f - ndv, 5.0f);
    float fresnel = 0.04f + 0.96f * edgeFresnel;
    float3 reflected = reflect(-V, N);
    float3 environment = StudioReflection(normalize(reflected)) *
        (Params.z * fresnel);

    color.rgb += specular + environment;
    return color; // untouched alpha, diffuse and texture sampling
}
