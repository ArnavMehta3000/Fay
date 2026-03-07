#undef min
#undef max

#include "Scene/GLTFImporter.h"
#include "Graphics/RendererBase.h"
#include "Graphics/GraphicsTypes.h"
#include "Scene/SceneGraph.h"
#include "Common/Log.h"
#include "Common/Profiling.h"
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

		// Loaded asset reference (lives for the duration of Load())
		fastgltf::Asset* Asset = nullptr;
		std::filesystem::path  BasePath;

		// Caches so we don't load the same texture twice
		std::vector<TextureResource> TextureCache;

		// ── Core loading methods ──
		std::unique_ptr<Scene> BuildScene();
		void LoadMeshes(Scene& scene);
		void LoadMaterials(Scene& scene);
		void LoadTextures();
		void BuildNodeHierarchy(Scene& scene);

		// ── Helpers ──
		TextureResource LoadTexture(u32 textureIndex);
		nvrhi::SamplerHandle CreateSampler(const fastgltf::Sampler& sampler);
		void ProcessNode(Scene& scene, SceneNode* parent, u32 nodeIndex);
	};


	GLTFImporter::GLTFImporter(Renderer* renderer)
		: m_impl(std::make_unique<Impl>())
	{
		m_impl->Renderer = renderer;
		m_impl->Device = renderer->GetDevice();
		m_impl->CmdList = m_impl->Device->createCommandList();
	}

	GLTFImporter::~GLTFImporter() = default;

	std::unique_ptr<Scene> GLTFImporter::Load(const fs::path& path)
	{
		ZoneScoped;

		Log::Info("Loading GLTF: {}", path.string());

		fastgltf::Parser parser(fastgltf::Extensions::KHR_materials_specular
			| fastgltf::Extensions::KHR_texture_transform 
			| fastgltf::Extensions::KHR_lights_punctual);

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
		
		std::unique_ptr<Scene> scene    = m_impl->BuildScene();
		m_impl->Asset = nullptr;
		m_impl->TextureCache.clear();

		Log::Info("Loaded glTF: {} meshes, {} materials, {} nodes",
			scene->Meshes.size(), scene->Materials.size(),
			m_impl->Asset ? 0 : scene->Meshes.size());

		return scene;
	}

	std::unique_ptr<Scene> GLTFImporter::Impl::BuildScene()
	{
		ZoneScoped;

		auto scene = std::make_unique<Scene>();

		LoadTextures();
		LoadMaterials(*scene);
		LoadMeshes(*scene);
		BuildNodeHierarchy(*scene);

		scene->UpdateTransforms();
		return scene;
	}

	void GLTFImporter::Impl::LoadMeshes(Scene& scene)
	{
		ZoneScoped;

		for (fastgltf::Mesh& gltfMesh : Asset->meshes)
		{
			auto mesh = std::make_unique<Mesh>();
			mesh->m_name = gltfMesh.name;

			std::vector<Vertex> allVertices;
			std::vector<u32> allIndices;

			for (auto& primitive : gltfMesh.primitives)
			{
				SubMesh sub;
				sub.VertexOffset  = static_cast<u32>(allVertices.size());
				sub.IndexOffset   = static_cast<u32>(allIndices.size());
				sub.MaterialIndex = primitive.materialIndex.has_value() ? static_cast<i32>(*primitive.materialIndex) : -1;

				// Positions
				fastgltf::Attribute* posAccessor = primitive.findAttribute("POSITION");
				if (!posAccessor)
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
				if (fastgltf::Attribute* normAttr = primitive.findAttribute("NORMAL"))
				{
					fastgltf::Accessor& acc = Asset->accessors[normAttr->accessorIndex];
					fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(*Asset, acc,
						[&](const fastgltf::math::fvec3& n, std::size_t i)
						{
							vertices[i].Normal = SM::Vector3(n.x(), n.y(), n.z());
						});
				}

				// UV
				if (fastgltf::Attribute* uvAttr = primitive.findAttribute("TEXCOORD_0"))
				{
					fastgltf::Accessor& acc = Asset->accessors[uvAttr->accessorIndex];
					fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(*Asset, acc,
						[&](const fastgltf::math::fvec2& uv, std::size_t i)
						{
							vertices[i].UV = SM::Vector2(uv.x(), uv.y());
						});
				}

				// Tangent
				//if (fastgltf::Attribute* tanAttr = primitive.findAttribute("TANGENT"))
				//{
				//	fastgltf::Accessor& acc = Asset->accessors[tanAttr->accessorIndex];
				//	fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(*Asset, acc,
				//		[&](const fastgltf::math::fvec4& t, std::size_t i)
				//		{
				//			vertices[i].Tangent = SM::Vector4(t.x(), t.y(), t.z(), t.w());
				//		});
				//}

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
			scene.Meshes.push_back(std::move(mesh));
		}
	}

	void GLTFImporter::Impl::LoadMaterials(Scene& scene)
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

			scene.Materials.push_back(std::move(mat));
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

		std::visit(fastgltf::visitor
		{
			[](auto&) { /* monostate / unhandled */ },

			[&](const fastgltf::sources::URI& uri)
			{
				fs::path fullPath = BasePath / uri.uri.fspath();
				pixels = stbi_load(fullPath.string().c_str(), &width, &height, &channels, 4);
			},

			[&](const fastgltf::sources::Array& array)
			{
				pixels = stbi_load_from_memory(
					reinterpret_cast<const stbi_uc*>(array.bytes.data()),
					static_cast<i32>(array.bytes.size()),
					&width, &height, &channels, 4);
			},

			[&](const fastgltf::sources::BufferView& bufferView)
			{
				fastgltf::BufferView& view = Asset->bufferViews[bufferView.bufferViewIndex];
				fastgltf::Buffer& buffer   = Asset->buffers[view.bufferIndex];
				
				std::visit(fastgltf::visitor
				{
					[](auto&) {},
					[&](const fastgltf::sources::Array& array)
					{
						pixels = stbi_load_from_memory(
							reinterpret_cast<const stbi_uc*>(array.bytes.data() + view.byteOffset),
							static_cast<i32>(view.byteLength),
							&width, &height, &channels, 4);
					}
				}, buffer.data);
			},
		}, gltfImage.data);

		if (!pixels)
		{
			Log::Warn("Failed to load texture image index {}", *gltfTexture.imageIndex);
			return {};
		}

		// Create GPU texture
		nvrhi::TextureDesc texDesc;
		texDesc.setWidth(width)
			.setHeight(height)
			.setFormat(nvrhi::Format::RGBA8_UNORM)
			.setDebugName(std::string(gltfImage.name.empty() ? "Texture" : gltfImage.name))
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

	void GLTFImporter::Impl::BuildNodeHierarchy(Scene& scene)
	{
		ZoneScoped;

		// Determine which glTF scene to use
		u64 sceneIndex = Asset->defaultScene.has_value() ? *Asset->defaultScene : 0;
		if (sceneIndex >= Asset->scenes.size())
		{
			Log::Warn("No valid scene in glTF, using all root nodes");
			for (u32 i = 0; i < Asset->nodes.size(); ++i)
			{
				ProcessNode(scene, scene.GetRoot(), i);
			}
			return;
		}

		fastgltf::Scene& gltfScene = Asset->scenes[sceneIndex];
		for (auto nodeIdx : gltfScene.nodeIndices)
		{
			ProcessNode(scene, scene.GetRoot(), static_cast<u32>(nodeIdx));
		}
	}

	void GLTFImporter::Impl::ProcessNode(Scene& scene, SceneNode* parent, u32 nodeIndex)
	{
		ZoneScoped;

		fastgltf::Node& gltfNode = Asset->nodes[nodeIndex];

		auto node = std::make_unique<SceneNode>(std::string(gltfNode.name));

		// Transform
        // NOTE: fastgltf stores TRS as a variant: either a matrix or decomposed TRS
		std::visit(fastgltf::visitor
		{
			[&](const fastgltf::TRS& trs)
			{
				node->LocalTransform.Translation = SM::Vector3(trs.translation[0], trs.translation[1], trs.translation[2]);
				node->LocalTransform.Rotation    = SM::Quaternion(trs.rotation[0], trs.rotation[1], trs.rotation[2], trs.rotation[3]);
				node->LocalTransform.Scale       = SM::Vector3(trs.scale[0], trs.scale[1], trs.scale[2]);
			},

			[&](const fastgltf::math::fmat4x4& mat)
			{
				// Decompose the 4x4 matrix into TRS
				SM::Matrix m(
					mat[0][0], mat[0][1], mat[0][2], mat[0][3],
					mat[1][0], mat[1][1], mat[1][2], mat[1][3],
					mat[2][0], mat[2][1], mat[2][2], mat[2][3],
					mat[3][0], mat[3][1], mat[3][2], mat[3][3]);
				m.Decompose(node->LocalTransform.Scale,
							node->LocalTransform.Rotation,
							node->LocalTransform.Translation);
			}
		}, gltfNode.transform);

		// Mesh component
		if (gltfNode.meshIndex.has_value())
		{
			node->AddComponent(MeshComponent{ .MeshIndex = static_cast<u32>(*gltfNode.meshIndex) });
		}

		// Camera component
		if (gltfNode.cameraIndex.has_value())
		{
			fastgltf::Camera& gltfCam = Asset->cameras[*gltfNode.cameraIndex];
			CameraComponent cam;

			std::visit(fastgltf::visitor
			{
				[&](const fastgltf::Camera::Perspective& persp)
				{
					cam.CameraType = CameraComponent::Type::Perspective;
					cam.FOV        = DirectX::XMConvertToDegrees(static_cast<f32>(persp.yfov));
					cam.NearPlane  = persp.znear;
					cam.FarPlane   = persp.zfar.has_value() ? static_cast<f32>(*persp.zfar) : 10000.0f;
				},

				[&](const fastgltf::Camera::Orthographic& ortho)
				{
					cam.CameraType  = CameraComponent::Type::Orthographic;
					cam.OrthoWidth  = static_cast<f32>(ortho.xmag * 2.0);
					cam.OrthoHeight = static_cast<f32>(ortho.ymag * 2.0);
					cam.NearPlane   = ortho.znear;
					cam.FarPlane    = ortho.zfar;
				}
			}, gltfCam.camera);

			node->AddComponent(std::move(cam));
		}

		// Light component (KHR_lights_punctual)
		if (gltfNode.lightIndex.has_value() && *gltfNode.lightIndex < Asset->lights.size())
		{
			fastgltf::Light& gltfLight = Asset->lights[*gltfNode.lightIndex];
			
			LightComponent light;

			switch (gltfLight.type)
			{
				using enum fastgltf::LightType;
			case Directional: light.LightType = LightComponent::Type::Directional; break;
			case Point:       light.LightType = LightComponent::Type::Point;       break;
			case Spot:        light.LightType = LightComponent::Type::Spot;        break;
			}

			light.Color     = SM::Vector3(gltfLight.color[0], gltfLight.color[1], gltfLight.color[2]);
			light.Intensity = gltfLight.intensity;

			if (gltfLight.range.has_value())
			{
				light.Range = static_cast<f32>(*gltfLight.range);
			}

			if (gltfLight.type == fastgltf::LightType::Spot)
			{
				light.InnerConeAngle = DirectX::XMConvertToDegrees(gltfLight.innerConeAngle.value_or(0.0f));
				light.OuterConeAngle = DirectX::XMConvertToDegrees(gltfLight.outerConeAngle.value_or(0.7854f));
			}

			node->AddComponent(std::move(light));
		}

		// ── Recurse into children ──
		SceneNode* rawNode = parent->AddChild(std::move(node));
		for (auto childIdx : gltfNode.children)
		{
			ProcessNode(scene, rawNode, static_cast<u32>(childIdx));
		}
	}
}