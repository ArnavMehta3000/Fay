#include <SDL3/SDL.h>
#include "Platform/Window.h"
#include "Common/Profiling.h"

int main()
{
    ZoneScoped;

    fay::Window window(fay::Window::Desc::Default());
    while (window.PumpEvents())
    {
        SDL_UpdateWindowSurface( window.GetSDL() );
        
        FrameMark;
    }
    return 0;
}
