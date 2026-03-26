#include "Common.hlsli"

ConstantBuffer<FrameConstantsCB> g_frameCB   : register(b0);
ConstantBuffer<ObjectConstantsCB> g_objectCB : register(b1);

void VSMain(
    float3 position          : POSITION,
    float3 normal            : NORMAL,
    float2 uv                : TEXCOORD0,
    float4 tangent           : TANGENT,
    out float3 outNormalWS   : NORMAL,
    out float2 outUV         : TEXCOORD0,
    out float4 outPositionCS : SV_POSITION,
    out float3 outPositionWS : TEXCOORD1)
{
    const float4 worldPos = mul(float4(position, 1.0f), g_objectCB.WorldMatrix);
    
    outPositionWS = worldPos.xyz;
    outPositionCS = mul(worldPos, g_frameCB.ViewProjMatrix);
    outNormalWS   = normalize(mul(normal, (float3x3) g_objectCB.NormalMatrix));
    outUV         = uv;
}