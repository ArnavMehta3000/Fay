#include "Graphics/ShaderLibrary.h"
#include "Platform/FileReader.h"
#include "Common/Log.h"

namespace fay
{
	ShaderLibrary::ShaderLibrary(const fs::path& shaderDir)
		: m_shaderDir(shaderDir)
	{
		// Iterate over shader directory, get file name (without extension) and add shader
		for (const auto& entry : fs::directory_iterator(shaderDir))
		{
			if (entry.path().extension() == ".cso")
			{
				AddShaderFromFile(entry.path().stem().string(), entry.path());
			}
		}
	}
	
	void ShaderLibrary::AddShaderFromFile(const std::string& name, const fs::path& filePath)
	{
		FileReader::ReadResult result = FileReader::Read(filePath);
		if (result)
		{
			m_shaders[name] = std::move(result.value());
			Log::Info("Added shader [{}] to library", name);
		}
		else
		{
			Log::Warn("Failed to add shader [{}] to library. Error: ", name, result.error());
		}
	}
	
	std::optional<ShaderBlob> ShaderLibrary::GetShaderBlob(const std::string& name) const
	{
		if (m_shaders.contains(name))
		{
			auto& data = m_shaders.at(name);
			return ShaderBlob{ .Data = data.data(), .Size = data.size() };
		}
		
		return std::nullopt;
	}
}