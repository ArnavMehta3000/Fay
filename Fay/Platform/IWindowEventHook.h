#pragma once

union SDL_Event;

namespace fay
{
    struct IWindowEventHook
    {
        virtual void OnWindowEvent(const SDL_Event&) = 0;
    };
}