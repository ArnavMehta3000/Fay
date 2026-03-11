#pragma once
#include <unordered_map>
#include <filesystem>
#include <vector>
#include <optional>
#include "Common/Types.h"

namespace fay
{
	namespace fs = std::filesystem;

	struct ShaderBlob
	{
		const byte* Data = nullptr;
		u64 Size = 0;
	};

	class ShaderLibrary
	{
	public:
		ShaderLibrary(const fs::path& shaderDir);

		void AddShaderFromFile(const std::string& name, const fs::path& filePath);
		std::optional<ShaderBlob> GetShaderBlob(const std::string& name) const;

	private:
		fs::path m_shaderDir;
		std::unordered_map<std::string, std::vector<byte>> m_shaders;  // Filename -> data
	};
}