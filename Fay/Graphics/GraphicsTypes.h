#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <nvrhi/nvrhi.h>
#include <SimpleMath.h>
#include "Common/Types.h"

namespace fay
{
	namespace SM = DirectX::SimpleMath;

	struct Vertex
	{
		SM::Vector3 Position;
		SM::Vector3 Normal;
		SM::Vector2 UV;
		SM::Vector4 Tangent;  // .w = handedness (+1 or -1)
	};

	struct BoundingBox
	{
		SM::Vector3 Min{ FLT_MAX,  FLT_MAX,  FLT_MAX };
		SM::Vector3 Max{ -FLT_MAX, -FLT_MAX, -FLT_MAX };

		void Expand(const SM::Vector3& point)
		{
			Min = SM::Vector3::Min(Min, point);
			Max = SM::Vector3::Max(Max, point);
		}

		[[nodiscard]] SM::Vector3 Center() const { return (Min + Max) * 0.5f; }
		[[nodiscard]] SM::Vector3 Extents() const { return (Max - Min) * 0.5f; }
	};

	static inline constexpr std::array<nvrhi::VertexAttributeDesc, 4> GetStandardVertexLayout()
	{
		return
		{
			nvrhi::VertexAttributeDesc()
				.setName("POSITION")
				.setFormat(nvrhi::Format::RGB32_FLOAT)
				.setOffset(offsetof(Vertex, Position))
				.setBufferIndex(0)
				.setElementStride(sizeof(Vertex)),

			nvrhi::VertexAttributeDesc()
				.setName("NORMAL")
				.setFormat(nvrhi::Format::RGB32_FLOAT)
				.setOffset(offsetof(Vertex, Normal))
				.setBufferIndex(0)
				.setElementStride(sizeof(Vertex)),

			nvrhi::VertexAttributeDesc()
				.setName("TEXCOORD")
				.setFormat(nvrhi::Format::RG32_FLOAT)
				.setOffset(offsetof(Vertex, UV))
				.setBufferIndex(0)
				.setElementStride(sizeof(Vertex)),

			nvrhi::VertexAttributeDesc()
				.setName("TANGENT")
				.setFormat(nvrhi::Format::RGBA32_FLOAT)
				.setOffset(offsetof(Vertex, Tangent))
				.setBufferIndex(0)
				.setElementStride(sizeof(Vertex)),
		};
	}

	struct TextureResource
	{
		nvrhi::TextureHandle Texture;
		nvrhi::SamplerHandle Sampler;

		[[nodiscard]] bool IsValid() const { return Texture != nullptr; }
	};

	enum class AlphaMode : u8
	{
		Opaque,
		Mask,
		Blend
	};

	struct MaterialConstants
	{
		SM::Vector4 BaseColorFactor   = { 1.0f, 1.0f, 1.0f, 1.0f };
		f32         MetallicFactor    = 1.0f;
		f32         RoughnessFactor   = 1.0f;
		f32         NormalScale       = 1.0f;
		f32         OcclusionStrength = 1.0f;
		SM::Vector3 EmissiveFactor    = { 0.0f, 0.0f, 0.0f };
		f32         AlphaCutoff       = 0.5f;
	};

	class Material
	{
		friend class GLTFImporter;

	public:
		Material() = default;
		~Material() = default;

		Material(const Material&) = delete;
		Material& operator=(const Material&) = delete;
		Material(Material&&) noexcept = default;
		Material& operator=(Material&&) noexcept = default;

		[[nodiscard]] inline std::string_view GetName() const { return m_name; }
		[[nodiscard]] inline AlphaMode GetAlphaMode() const { return m_alphaMode; }
		[[nodiscard]] inline bool IsDoubleSided() const { return m_doubleSided; }
		[[nodiscard]] inline const MaterialConstants& GetConstants() const { return m_constants; }

		[[nodiscard]] inline const TextureResource& GetBaseColorTexture() const { return m_baseColor; }
		[[nodiscard]] inline const TextureResource& GetMetallicRoughnessTexture() const { return m_metallicRoughness; }
		[[nodiscard]] inline const TextureResource& GetNormalTexture() const { return m_normal; }
		[[nodiscard]] inline const TextureResource& GetOcclusionTexture() const { return m_occlusion; }
		[[nodiscard]] inline const TextureResource& GetEmissiveTexture() const { return m_emissive; }

		[[nodiscard]] inline nvrhi::IBuffer* GetConstantBuffer() const { return m_constantBuffer; }
		[[nodiscard]] inline nvrhi::IBindingSet* GetBindingSet() const { return m_bindingSet; }

	private:
		std::string       m_name;
		AlphaMode         m_alphaMode = AlphaMode::Opaque;
		bool              m_doubleSided = false;
		MaterialConstants m_constants;

		TextureResource m_baseColor;
		TextureResource m_metallicRoughness;
		TextureResource m_normal;
		TextureResource m_occlusion;
		TextureResource m_emissive;

		nvrhi::BufferHandle     m_constantBuffer;
		nvrhi::BindingSetHandle m_bindingSet;
	};

	struct Transform
	{
		SM::Vector3    Translation = SM::Vector3::Zero;
		SM::Quaternion Rotation = SM::Quaternion::Identity;
		SM::Vector3    Scale = SM::Vector3::One;

		[[nodiscard]] inline SM::Matrix ToLocalMatrix() const
		{
			return SM::Matrix::CreateScale(Scale)
				* SM::Matrix::CreateFromQuaternion(Rotation)
				* SM::Matrix::CreateTranslation(Translation);
		}
	};

	struct MeshComponent
	{
		u32 MeshIndex = 0;  // index into Scene::Meshes
	};

	struct CameraComponent
	{
		enum class Type { Perspective, Orthographic };
		Type CameraType = Type::Perspective;

		f32 FOV = 45.0f;   // degrees
		f32 NearPlane = 0.1f;
		f32 FarPlane = 1000.0f;

		f32 OrthoWidth = 10.0f;
		f32 OrthoHeight = 10.0f;

		[[nodiscard]] SM::Matrix GetProjectionMatrix(f32 aspectRatio) const
		{
			if (CameraType == Type::Perspective)
			{
				return SM::Matrix::CreatePerspectiveFieldOfView(
					DirectX::XMConvertToRadians(FOV), aspectRatio, NearPlane, FarPlane);
			}
			return SM::Matrix::CreateOrthographic(OrthoWidth, OrthoHeight, NearPlane, FarPlane);
		}
	};

	struct LightComponent
	{
		enum class Type { Directional, Point, Spot };
		Type LightType = Type::Directional;

		SM::Vector3 Color = { 1.0f, 1.0f, 1.0f };
		f32 Intensity = 1.0f;

		// Point / Spot
		f32 Range = 10.0f;

		// Spot only
		f32 InnerConeAngle = 30.0f;  // degrees
		f32 OuterConeAngle = 45.0f;  // degrees
	};
}