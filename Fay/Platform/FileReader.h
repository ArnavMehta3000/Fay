#pragma once
#include <filesystem>
#include <vector>
#include <expected>
#include "Common/Types.h"

namespace fay
{
	namespace fs = std::filesystem;

	class FileReader
	{
	public:
		using ReadResult = std::expected<std::vector<byte>, std::string>;

		static ReadResult Read(const fs::path& path);
	};
}