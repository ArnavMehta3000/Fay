#pragma once
#include <memory>
#include <filesystem>

namespace fay
{
	namespace fs = std::filesystem;

	class Renderer;
	class Scene;

	class GLTFImporter
	{
	public:
		explicit GLTFImporter(Renderer* renderer);
		~GLTFImporter();

		GLTFImporter(const GLTFImporter&) = delete;
		GLTFImporter& operator=(const GLTFImporter&) = delete;

		[[nodiscard]] std::unique_ptr<Scene> Load(const fs::path& path);

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;
	};
}