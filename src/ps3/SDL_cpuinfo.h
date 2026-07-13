#ifndef PS3_SDL_CPUINFO_H
#define PS3_SDL_CPUINFO_H

#include "SDL.h"

SDL_bool SDL_HasRDTSC(void);
SDL_bool SDL_HasMMX(void);
SDL_bool SDL_Has3DNow(void);
SDL_bool SDL_HasSSE(void);
SDL_bool SDL_HasSSE2(void);
SDL_bool SDL_HasAltiVec(void);

#endif
