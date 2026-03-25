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
}