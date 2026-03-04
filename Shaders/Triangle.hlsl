cbuffer CB : register(b0)
{
	float4x4 g_Transform;
};

void VSMain(
	float3 i_pos     : POSITION,
    float2 i_uv      : UV,
	out float4 o_pos : SV_Position,
	out float2 o_uv  : UV
)
{
	o_pos = mul(float4(i_pos, 1), g_Transform);
	o_uv = i_uv;
}




void PSMain(
	in float4 i_pos    : SV_Position,
	in float2 i_uv     : UV,
	out float4 o_color : SV_Target0
)
{
	o_color = float4(i_uv.xy, 0.0f, 1.0f);
}