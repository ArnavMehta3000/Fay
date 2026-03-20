struct FrameConstantsCB
{
	float4x4 ViewMatrix;
	float4x4 ProjMatrix;
	float4x4 ViewProjMatrix;
	float3 CameraPosition;
	float Time;
};

struct ObjectConstantsCB
{
    float4x4 WorldMatrix;
    float4x4 NormalMatrix; // inverse-transpose of WorldMatrix (upper 3x3, padded to 4x4)
};
