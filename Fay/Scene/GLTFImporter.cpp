#undef min
#undef max

#include "Scene/GLTFImporter.h"
#include "Graphics/RendererBase.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/Mesh.h"
#include "Common/Log.h"
#include "Common/Profiling.h"
#include "Common/Assert.h"
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <nvrhi/utils.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace fay
{
	namespace fs = std::filesystem;

	struct GLTFImporter::Impl
	{
		Renderer* Renderer = nullptr;
		nvrhi::IDevice* Device = nullptr;
		nvrhi::CommandListHandle CmdList;

		fastgltf::Asset* Asset = nullptr;
		std::filesystem::path  BasePath;

		std::vector<TextureResource> TextureCache;

		std::unique_ptr<MeshCollection> BuildMeshCollection();
		void LoadMeshes(MeshCollection& collection);
		void LoadMaterials(MeshCollection& collection);
		void LoadTextures();

		TextureResource LoadTexture(u32 textureIndex);
		nvrhi::SamplerHandle CreateSampler(const fastgltf::Sampler& sampler);
	};


	GLTFImporter::GLTFImporter(Renderer* renderer)
		: m_impl(std::make_unique<Impl>())
	{
		m_impl->Renderer = renderer;
		m_impl->Device = renderer->GetDevice();
		m_impl->CmdList = m_impl->Device->createCommandList();
	}

	GLTFImporter::~GLTFImporter() = default;

	std::unique_ptr<MeshCollection> GLTFImporter::Load(const fs::path& path)
	{
		ZoneScoped;

		Log::Info("Loading GLTF: {}", path.string());

		fastgltf::Parser parser(fastgltf::Extensions::KHR_materials_variants
			| fastgltf::Extensions::KHR_texture_transform
			| fastgltf::Extensions::KHR_mesh_quantization);

		auto data = fastgltf::GltfDataBuffer::FromPath(path);

		if (data.error() != fastgltf::Error::None)
		{
			Log::Error("fastgltf: failed to read file: {}", fastgltf::getErrorMessage(data.error()));
			return nullptr;
		}

		m_impl->BasePath = path.parent_path();

		constexpr fastgltf::Options options = fastgltf::Options::LoadExternalBuffers
			| fastgltf::Options::LoadExternalImages
			| fastgltf::Options::GenerateMeshIndices;

		fastgltf::GltfType type = fastgltf::determineGltfFileType(data.get());

		// Load GLTF or GLB
		auto asset = (type == fastgltf::GltfType::glTF)
			? parser.loadGltf(data.get(), m_impl->BasePath, options)
			: parser.loadGltfBinary(data.get(), m_impl->BasePath, options);

		if (asset.error() != fastgltf::Error::None)
		{
			Log::Error("fastgltf: failed to parse: {}", fastgltf::getErrorMessage(asset.error()));
			return nullptr;
		}

		m_impl->Asset = &asset.get();

		std::unique_ptr<MeshCollection> collection = m_impl->BuildMeshCollection();
		
		collection->SourcePath = path.string();
		collection->Textures = std::move(m_impl->TextureCache);

		m_impl->Asset = nullptr;
		m_impl->TextureCache.clear();

		Log::Info("Loaded glTF: {} meshes, {} materials, {} textures",
			collection->Meshes.size(), collection->Materials.size(), collection->Textures.size());

		return collection;
	}

	std::unique_ptr<MeshCollection> GLTFImporter::Impl::BuildMeshCollection()
	{
		ZoneScoped;

		auto collection = std::make_unique<MeshCollection>();

		LoadTextures();
		LoadMaterials(*collection);
		LoadMeshes(*collection);

		return collection;
	}

	void GLTFImporter::Impl::LoadMeshes(MeshCollection& collection)
	{
		ZoneScoped;

		// For-each mesh
		for (fastgltf::Mesh& gltfMesh : Asset->meshes)
		{
			auto mesh = std::make_unique<Mesh>();
			mesh->m_name = gltfMesh.name;

			std::vector<Vertex> allVertices;
			std::vector<u32> allIndices;

			// For-each primitive in mesh
			for (auto& primitive : gltfMesh.primitives)
			{
				SubMesh sub;
				sub.VertexOffset  = static_cast<u32>(allVertices.size());
				sub.IndexOffset   = static_cast<u32>(allIndices.size());
				sub.MaterialIndex = primitive.materialIndex.has_value() ? static_cast<i32>(*primitive.materialIndex) : -1;

				// Positions
				fastgltf::Attribute* posAccessor = primitive.findAttribute("POSITION");
				if (!posAccessor || posAccessor == primitive.attributes.end())
				{
					Log::Warn("Primitive in mesh '{}' has no POSITION attribute, skipping", mesh->m_name);
					continue;
				}

				fastgltf::Accessor& posAcc = Asset->accessors[posAccessor->accessorIndex];
				const u32 vertexCount = static_cast<u32>(posAcc.count);

				allVertices.resize(allVertices.size() + vertexCount);
				Vertex* vertices = &allVertices[sub.VertexOffset];

				// Read positions
				fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(*Asset, posAcc,
					[&](const fastgltf::math::fvec3& pos, std::size_t i)
					{
						vertices[i].Position = SM::Vector3(pos.x(), pos.y(), pos.z());
						sub.Bounds.Expand(vertices[i].Position);
					});

				// Normals
				if (fastgltf::Attribute* normAttr = primitive.findAttribute("NORMAL"); normAttr != primitive.attributes.end())
				{
					fastgltf::Accessor& acc = Asset->accessors[normAttr->accessorIndex];
					fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(*Asset, acc,
						[&](const fastgltf::math::fvec3& n, std::size_t i)
						{
							vertices[i].Normal = SM::Vector3(n.x(), n.y(), n.z());
						});
				}
				else
				{
					Log::Warn("Primitive in mesh '{}' has no NORMAL attribute", mesh->m_name);
				}

				// UV
				if (fastgltf::Attribute* uvAttr = primitive.findAttribute("TEXCOORD_0"); uvAttr != primitive.attributes.end())
				{
					fastgltf::Accessor& acc = Asset->accessors[uvAttr->accessorIndex];
					fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(*Asset, acc,
						[&](const fastgltf::math::fvec2& uv, std::size_t i)
						{
							vertices[i].UV = SM::Vector2(uv.x(), uv.y());
						});
				}
				else
				{
					Log::Warn("Primitive in mesh '{}' has no TEXCOORD_0 attribute", mesh->m_name);
				}

				// Tangent
				bool hasTangent = false;
				if (fastgltf::Attribute* tanAttr = primitive.findAttribute("TANGENT"); tanAttr != primitive.attributes.end())
				{
					fastgltf::Accessor& acc = Asset->accessors[tanAttr->accessorIndex];
					fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(*Asset, acc,
						[&](const fastgltf::math::fvec4& t, std::size_t i)
						{
							vertices[i].Tangent = SM::Vector4(t.x(), t.y(), t.z(), t.w());
						});
					hasTangent = true;
				}
				else
				{
					Log::Warn("Primitive in mesh '{}' has no TANGENT attribute", mesh->m_name);
				}

				// Indices
				if (primitive.indicesAccessor.has_value())
				{
					fastgltf::Accessor& idxAcc = Asset->accessors[*primitive.indicesAccessor];
					sub.IndexCount = static_cast<u32>(idxAcc.count);
					allIndices.reserve(allIndices.size() + sub.IndexCount);

					fastgltf::iterateAccessor<u32>(*Asset, idxAcc,
						[&](u32 idx)
						{
							allIndices.push_back(idx);
						});
				}
				else
				{
					// Non-indexed: generate sequential indices
					sub.IndexCount = vertexCount;
					for (u32 i = 0; i < vertexCount; ++i)
					{
						allIndices.push_back(i);
					}
				}

				if (!hasTangent)
				{
					// Since no tangents were found, generate them
					//TODO: MikkTSpace tangent generation
					for (u32 i = 0; i < vertexCount; ++i)
					{
						vertices[i].Tangent = SM::Vector4::UnitX;
					}
				}

				mesh->m_bounds.Expand(sub.Bounds.Min);
				mesh->m_bounds.Expand(sub.Bounds.Max);
				mesh->m_subMeshes.push_back(std::move(sub));
			}

			mesh->m_vertexCount = static_cast<u32>(allVertices.size());
			mesh->m_indexCount = static_cast<u32>(allIndices.size());

			// Upload to GPU
			if (!allVertices.empty())
			{
				const size_t vbSize = allVertices.size() * sizeof(Vertex);
				const size_t ibSize = allIndices.size() * sizeof(uint32_t);

				mesh->m_vertexBuffer = Device->createBuffer(nvrhi::BufferDesc()
					.setByteSize(vbSize)
					.setIsVertexBuffer(true)
					.setDebugName(mesh->m_name + "_VB")
					.setInitialState(nvrhi::ResourceStates::CopyDest));

				mesh->m_indexBuffer = Device->createBuffer(nvrhi::BufferDesc()
					.setByteSize(ibSize)
					.setIsIndexBuffer(true)
					.setDebugName(mesh->m_name + "_IB")
					.setInitialState(nvrhi::ResourceStates::CopyDest));

				CmdList->open();

				CmdList->beginTrackingBufferState(mesh->m_vertexBuffer, nvrhi::ResourceStates::CopyDest);
				CmdList->writeBuffer(mesh->m_vertexBuffer, allVertices.data(), vbSize);
				CmdList->setPermanentBufferState(mesh->m_vertexBuffer, nvrhi::ResourceStates::VertexBuffer);

				CmdList->beginTrackingBufferState(mesh->m_indexBuffer, nvrhi::ResourceStates::CopyDest);
				CmdList->writeBuffer(mesh->m_indexBuffer, allIndices.data(), ibSize);
				CmdList->setPermanentBufferState(mesh->m_indexBuffer, nvrhi::ResourceStates::IndexBuffer);

				CmdList->close();
				Device->executeCommandList(CmdList);
			}
			collection.Meshes.push_back(std::move(mesh));
		}
	}

	void GLTFImporter::Impl::LoadMaterials(MeshCollection& collection)
	{
		ZoneScoped;

		for (fastgltf::Material& gltfMat : Asset->materials)
		{
			auto mat = std::make_unique<Material>();
			mat->m_name = std::string(gltfMat.name);
			mat->m_doubleSided = gltfMat.doubleSided;

			switch (gltfMat.alphaMode)
			{
				using enum fastgltf::AlphaMode;
			case Mask:  mat->m_alphaMode = AlphaMode::Mask;  break;
			case Blend: mat->m_alphaMode = AlphaMode::Blend; break;
			default:    mat->m_alphaMode = AlphaMode::Opaque; break;
			}

			mat->m_constants.AlphaCutoff = gltfMat.alphaCutoff;

			// PBR metallic-roughness
			auto& pbr = gltfMat.pbrData;
			mat->m_constants.BaseColorFactor = SM::Vector4(
				pbr.baseColorFactor[0], pbr.baseColorFactor[1],
				pbr.baseColorFactor[2], pbr.baseColorFactor[3]);
			mat->m_constants.MetallicFactor = pbr.metallicFactor;
			mat->m_constants.RoughnessFactor = pbr.roughnessFactor;

			// Textures
			if (pbr.baseColorTexture.has_value())
			{
				mat->m_baseColor = LoadTexture(static_cast<u32>(pbr.baseColorTexture->textureIndex));
			}

			if (pbr.metallicRoughnessTexture.has_value())
			{
				mat->m_metallicRoughness = LoadTexture(static_cast<u32>(pbr.metallicRoughnessTexture->textureIndex));
			}

			if (gltfMat.normalTexture.has_value())
			{
				mat->m_normal = LoadTexture(static_cast<u32>(gltfMat.normalTexture->textureIndex));
				mat->m_constants.NormalScale = gltfMat.normalTexture->scale;
			}

			if (gltfMat.occlusionTexture.has_value())
			{
				mat->m_occlusion = LoadTexture(static_cast<u32>(gltfMat.occlusionTexture->textureIndex));
				mat->m_constants.OcclusionStrength = gltfMat.occlusionTexture->strength;
			}

			if (gltfMat.emissiveTexture.has_value())
				mat->m_emissive = LoadTexture(static_cast<u32>(gltfMat.emissiveTexture->textureIndex));

			mat->m_constants.EmissiveFactor = SM::Vector3(
				gltfMat.emissiveFactor[0], gltfMat.emissiveFactor[1], gltfMat.emissiveFactor[2]);

			// Create material constant buffer
			mat->m_constantBuffer = Device->createBuffer(
				nvrhi::utils::CreateStaticConstantBufferDesc(sizeof(MaterialConstants), (mat->m_name + "_CB").c_str())
				.setInitialState(nvrhi::ResourceStates::ConstantBuffer)
				.setKeepInitialState(true));

			CmdList->open();
			CmdList->writeBuffer(mat->m_constantBuffer, &mat->m_constants, sizeof(MaterialConstants));
			CmdList->close();
			Device->executeCommandList(CmdList);

			collection.Materials.push_back(std::move(mat));
		}
	}

	void GLTFImporter::Impl::LoadTextures()
	{
		ZoneScoped;
		TextureCache.resize(Asset->textures.size());
	}

	TextureResource GLTFImporter::Impl::LoadTexture(u32 textureIndex)
	{
		ZoneScoped;

		if (textureIndex >= Asset->textures.size())
		{
			return {};
		}

		// Return cached if already loaded
		if (TextureCache[textureIndex].IsValid())
		{
			return TextureCache[textureIndex];
		}

		fastgltf::Texture& gltfTexture = Asset->textures[textureIndex];
		if (!gltfTexture.imageIndex.has_value())
		{
			return {};
		}

		fastgltf::Image& gltfImage = Asset->images[*gltfTexture.imageIndex];

		// Decode image
		i32 width = 0, height = 0, channels = 0;
		stbi_uc* pixels = nullptr;

		if (std::holds_alternative<fastgltf::sources::URI>(gltfImage.data))
		{
			auto& uri = std::get<fastgltf::sources::URI>(gltfImage.data);  // Load from filename

			Assert(uri.fileByteOffset == 0);
			Assert(uri.uri.isLocalPath());  // Needs to be a local file to be loaded

			fs::path fullPath = BasePath / uri.uri.fspath();
			pixels = stbi_load(fullPath.string().c_str(), &width, &height, &channels, 4);
		}
		else if (std::holds_alternative<fastgltf::sources::Array>(gltfImage.data))  // Load from memory
		{
			auto& array = std::get<fastgltf::sources::Array>(gltfImage.data);
			pixels = stbi_load_from_memory(
				reinterpret_cast<const stbi_uc*>(array.bytes.data()),
				static_cast<i32>(array.bytes.size()),
				&width, &height, &channels, 4);
		}
		else if (std::holds_alternative<fastgltf::sources::BufferView>(gltfImage.data))  // Load from buffer view
		{
			auto& bufferView = std::get<fastgltf::sources::BufferView>(gltfImage.data);

			fastgltf::BufferView& view = Asset->bufferViews[bufferView.bufferViewIndex];
			fastgltf::Buffer& buffer = Asset->buffers[view.bufferIndex];

			if (std::holds_alternative<fastgltf::sources::Array>(buffer.data))
			{
				auto& array = std::get<fastgltf::sources::Array>(buffer.data);
				pixels = stbi_load_from_memory(
					reinterpret_cast<const stbi_uc*>(array.bytes.data()),
					static_cast<i32>(array.bytes.size()),
					&width, &height, &channels, 4);
			}
		}
		else
		{
			Assert(!"Unknown image data type");
		}

		if (!pixels)
		{
			Log::Warn("Failed to load texture image index {}", *gltfTexture.imageIndex);
			return {};
		}

		// Create GPU texture
		auto texDesc = nvrhi::TextureDesc()
			.setDimension(nvrhi::TextureDimension::Texture2D)
			.setWidth(width)
			.setHeight(height)
			.setFormat(nvrhi::Format::SRGBA8_UNORM)
			.setDebugName(std::string(gltfImage.name.empty() ? "GLTFTexture" : gltfImage.name))
			.setInitialState(nvrhi::ResourceStates::CopyDest)
			.setKeepInitialState(false);

		nvrhi::TextureHandle texture = Device->createTexture(texDesc);

		CmdList->open();
		CmdList->beginTrackingTextureState(texture, nvrhi::AllSubresources, nvrhi::ResourceStates::CopyDest);
		CmdList->writeTexture(texture, 0, 0, pixels, static_cast<size_t>(width) * 4);
		CmdList->setPermanentTextureState(texture, nvrhi::ResourceStates::ShaderResource);
		CmdList->close();
		Device->executeCommandList(CmdList);

		stbi_image_free(pixels);

		// Create sampler
		nvrhi::SamplerHandle sampler;
		if (gltfTexture.samplerIndex.has_value())
		{
			sampler = CreateSampler(Asset->samplers[*gltfTexture.samplerIndex]);
		}
		else
		{
			sampler = Device->createSampler(nvrhi::SamplerDesc()
				.setAllFilters(true)
				.setAllAddressModes(nvrhi::SamplerAddressMode::Repeat));
		}

		TextureResource result{ texture, sampler };
		TextureCache[textureIndex] = result;
		return result;
	}

	nvrhi::SamplerHandle GLTFImporter::Impl::CreateSampler(const fastgltf::Sampler& sampler)
	{
		nvrhi::SamplerDesc desc;

		// Min filter
		if (sampler.minFilter.has_value())
		{
			switch (*sampler.minFilter)
			{
			case fastgltf::Filter::Nearest:
			case fastgltf::Filter::NearestMipMapNearest:
			case fastgltf::Filter::NearestMipMapLinear:
				desc.setMinFilter(false);
				break;
			default:
				desc.setMinFilter(true);
				break;
			}
		}

		// Mag filter
		if (sampler.magFilter.has_value())
		{
			desc.setMagFilter(*sampler.magFilter == fastgltf::Filter::Linear);
		}

		// Wrap modes
		auto toAddressMode = [](fastgltf::Wrap wrap) -> nvrhi::SamplerAddressMode
			{
				switch (wrap)
				{
				case fastgltf::Wrap::ClampToEdge:    return nvrhi::SamplerAddressMode::ClampToEdge;
				case fastgltf::Wrap::MirroredRepeat: return nvrhi::SamplerAddressMode::MirroredRepeat;
				default:                             return nvrhi::SamplerAddressMode::Repeat;
				}
			};

		desc.setAddressU(toAddressMode(sampler.wrapS));
		desc.setAddressV(toAddressMode(sampler.wrapT));

		return Device->createSampler(desc);
	}
}