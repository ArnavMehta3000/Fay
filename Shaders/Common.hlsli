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
    float4x4 NormalMatrix;
};

struct MaterialConstantsCB
{
    float4 BaseColorFactor;
    float MetallicFactor;
    float RoughnessFactor;
    float NormalScale;
    float OcclusionStrength;
    float3 EmissiveFactor;
    float AlphaCutoff;
};