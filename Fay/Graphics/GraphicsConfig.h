#pragma once
#include "Common/Macros.h"

#if FAY_OS_WINDOWS
	#define FAY_HAS_D3D 1
	#define FAY_HAS_VULKAN 1
#else
	#define FAY_HAS_D3D 0
#endif

#if FAY_OS_LINUX
	#define FAY_HAS_VULKAN 1
#endif