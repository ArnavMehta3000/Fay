SamplerState g_texSampler : register(s0);
Texture2D g_tex           : register(t0);

float4 PSMain(
	float4 position : SV_POSITION,
	float2 uv       : TEXCOORD0,
	float4 color    : COLOR0) : SV_Target
{
    return g_tex.Sample(g_texSampler, uv) * color;
}
