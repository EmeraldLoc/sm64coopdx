#ifndef GFX_SDL_H
#define GFX_SDL_H

#include <SDL2/SDL.h>

#include "gfx_window_manager_api.h"

extern struct GfxWindowManagerAPI gfx_sdl;
SDL_Window *gfx_sdl_get_window();

bool gfx_sdl_check_opengl_compatibility(void);

#endif
