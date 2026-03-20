#pragma once
#include "Common/Macros.h"

#if FAY_DEBUG
#define SDL_ASSERT_LEVEL 2
#else
#define SDL_ASSERT_LEVEL 1
#endif

#include <SDL3/SDL_assert.h>

#define Assert(...) SDL_assert(__VA_ARGS__)
