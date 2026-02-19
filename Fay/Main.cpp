#include <SDL3/SDL.h>

SDL_Window* g_window = nullptr;
SDL_Surface* g_screenSurface = nullptr;

int main()
{
    // Init SDL
    if (SDL_Init(SDL_INIT_VIDEO) == false)
    {
        fay::Log::Error("SDL could not initialize! SDL error: {}", SDL_GetError());
    }
     
    if (g_window = SDL_CreateWindow("Fay Renderer", 1280, 720, 0); !g_window)
    {
        fay::Log::Error("Failed to create window! SDL Error: {}", SDL_GetError());
    }
    else
    {
        g_screenSurface = SDL_GetWindowSurface(g_window);
    }

    bool quit = false;
    SDL_Event e;
    SDL_zero(e);

    while (!quit)
    {
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_EVENT_QUIT)
                quit = true;
        }
        
        //Fill the surface white
        SDL_FillSurfaceRect( g_screenSurface, nullptr, SDL_MapSurfaceRGB( g_screenSurface, 0xFF, 0xFF, 0xFF ) );
    
        //Update the surface
        SDL_UpdateWindowSurface( g_window );
    }
    

    SDL_DestroySurface(g_screenSurface);
    SDL_DestroyWindow(g_window);
    SDL_Quit();

    
    return 0;
}
