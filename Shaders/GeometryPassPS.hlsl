#include "Common.hlsli"

ConstantBuffer<MaterialConstantsCB> g_materialCB : register(b0, space1);

Texture2D g_baseColorMap         : register(t0, space1);
Texture2D g_metallicRoughnessMap : register(t1, space1);
Texture2D g_normalMap            : register(t2, space1);
Texture2D g_occlusionMap         : register(t3, space1);
Texture2D g_emissiveMap          : register(t4, space1);

SamplerState g_baseColorSampler         : register(s0, space1);
SamplerState g_metallicRoughnessSampler : register(s1, space1);
SamplerState g_normalSampler            : register(s2, space1);
SamplerState g_occlusionSampler         : register(s3, space1);
SamplerState g_emissiveSampler          : register(s4, space1);

float4 PSMain(
    float3 normalWS   : NORMAL,
    float2 uv         : TEXCOORD0,
    float4 positionCS : SV_POSITION,
    float3 positionWS : TEXCOORD1) : SV_Target
{
    const float3 LightDir = normalize(float3(0.5, 1.0, 0.3));
    const float Ambient = 0.15;
    
    float3 N = normalize(normalWS);
    float3 normalColor = N * 0.5 + 0.5;
    
    float NdotL = saturate(dot(N, LightDir));
    float lighting = Ambient + (1.0 - Ambient) * NdotL;
    
    float4 texCol = g_baseColorMap.Sample(g_baseColorSampler, uv);
    
    return float4(normalColor * lighting, 1.0f) * texCol;
}