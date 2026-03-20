#include "Common.hlsli"

static const float3 LightDir = normalize(float3(0.5, 1.0, 0.3));
static const float Ambient = 0.15;

ConstantBuffer<FrameConstantsCB> g_frameCB : register(b0);
ConstantBuffer<ObjectConstantsCB> g_objectCB : register(b1);

struct VSInput
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
	float2 UV : TEXCOORD;
	float4 Tangent : TANGENT;
};

struct VSOutput
{
	float4 PositionCS : SV_Position;
	float3 NormalWS : NORMAL;
	float3 PositionWS : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
	VSOutput output;

	float4 worldPos   = mul(float4(input.Position, 1.0), g_objectCB.WorldMatrix);
	output.PositionWS = worldPos.xyz;
	output.PositionCS = mul(worldPos, g_frameCB.ViewProjMatrix);
	output.NormalWS   = normalize(mul(input.Normal, (float3x3) g_objectCB.NormalMatrix));

	return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
	float3 N = normalize(input.NormalWS);

	// Remap normal from [-1,1] to [0,1] for visualization
	float3 normalColor = N * 0.5 + 0.5;

	// Simple NdotL so you can see the shape
	float NdotL = saturate(dot(N, LightDir));
	float lighting = Ambient + (1.0 - Ambient) * NdotL;
	return float4(normalColor * lighting, 1.0);
}