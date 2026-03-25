#pragma once
#include <memory>
#include <filesystem>
#include <vector>
#include <string>
#include "Graphics/GraphicsTypes.h"

namespace fay
{
	namespace fs = std::filesystem;

	class Renderer;
	class Mesh;
	class Material;

	struct MeshCollection
	{
		std::string SourcePath;
		std::vector<std::unique_ptr<Mesh>> Meshes;
		std::vector<std::unique_ptr<Material>> Materials;
		std::vector<TextureResource> Textures;
	};

	class GLTFImporter
	{
	public:
		explicit GLTFImporter(Renderer* renderer);
		~GLTFImporter();

		GLTFImporter(const GLTFImporter&) = delete;
		GLTFImporter& operator=(const GLTFImporter&) = delete;

		[[nodiscard]] std::unique_ptr<MeshCollection> Load(const fs::path& path);

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;
	};
}