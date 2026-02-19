#pragma once
#include <SDL3/SDL_assert.h>

namespace fay
{
    // Inline function that calls SDL's assert
    inline void Assert([[maybe_unused]] auto condition)
    {
        SDL_assert(condition);
    }
}