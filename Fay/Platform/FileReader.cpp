#include "Platform/FileReader.h"
#include <fstream>

namespace fay
{
	FileReader::ReadResult FileReader::Read(const fs::path& path)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file.is_open())
		{
			return std::unexpected("Failed to open file");
		}

		std::vector<byte> buffer(file.tellg());
		file.seekg(0, std::ios::beg);
		file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
		return buffer;
	}
}