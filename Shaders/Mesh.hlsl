#include "Common.hlsli"

static const float3 LightDir = normalize(float3(0.5, 1.0, 0.3));
static const float Ambient = 0.15;

ConstantBuffer<FrameConstantsCB> g_frameCB : register(b0);
ConstantBuffer<ObjectConstantsCB> g_objectCB : register(b1);

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

struct VSInput
{
	float3 Position : POSITION;
	float3 Normal   : NORMAL;
	float2 UV       : TEXCOORD;
	float4 Tangent  : TANGENT;
};

struct VSOutput
{
	float4 PositionCS : SV_Position;
	float3 NormalWS   : NORMAL;
	float3 PositionWS : TEXCOORD0;
	float2 UV         : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
	VSOutput output;

	float4 worldPos   = mul(float4(input.Position, 1.0), g_objectCB.WorldMatrix);
	output.PositionWS = worldPos.xyz;
	output.PositionCS = mul(worldPos, g_frameCB.ViewProjMatrix);
	output.NormalWS   = normalize(mul(input.Normal, (float3x3) g_objectCB.NormalMatrix));
	output.UV         = input.UV;

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
	
    return g_baseColorMap.Sample(g_baseColorSampler, input.UV);
	//return float4(normalColor * lighting, 1.0);
}