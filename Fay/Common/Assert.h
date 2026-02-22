#pragma once
#include "Common/Macros.h"

#if FAY_DEBUG
#define SDL_ASSERT_LEVEL 2
#else
#define SDL_ASSERT_LEVEL 1
#endif

#include <SDL3/SDL_assert.h>

namespace fay
{
    // Inline function that calls SDL's assert
    inline void Assert([[maybe_unused]] auto condition)
    {
        SDL_assert(condition);
    }
}