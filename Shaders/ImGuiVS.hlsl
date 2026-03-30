struct Constants
{
	float2 InvDisplaySize;
};

ConstantBuffer<Constants> g_constants : register(b0);

void VSMain(
	float2 position         : POSITION,
	float2 uv               : TEXCOORD0,
	float4 color            : COLOR0,
	out float4 outPosition  : SV_POSITION,
	out float2 outUV        : TEXCOORD0,
	out float4 outColor     : COLOR0)
{
	outPosition.xy = position.xy * g_constants.InvDisplaySize * float2(2.0, -2.0) + float2(-1.0, 1.0);
	outPosition.zw = float2(0.0f, 1.0f);
	outColor       = color;
	outUV          = uv;
}
