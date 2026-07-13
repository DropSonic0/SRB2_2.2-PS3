#include "SDL.h"
#include "SDL_cpuinfo.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef __CELLOS_LV2__
#include <sys/timer.h>
#include <unistd.h>
#endif

// We need to implement SDLforceUngrabMouse since i_system.c calls it
void SDLforceUngrabMouse(void) {}

void SDL_JoystickClose(SDL_Joystick *dev) { (void)dev; }
int SDL_NumJoysticks(void) { return 0; }
SDL_Joystick *SDL_JoystickOpen(int device_index) { (void)device_index; return NULL; }
SDL_bool SDL_JoystickGetAttached(SDL_Joystick *dev) { (void)dev; return SDL_FALSE; }
const char *SDL_JoystickName(SDL_Joystick *dev) { (void)dev; return ""; }
const char *SDL_JoystickNameForIndex(int device_index) { (void)device_index; return NULL; }
int SDL_JoystickNumAxes(SDL_Joystick *dev) { (void)dev; return 0; }
int SDL_JoystickNumButtons(SDL_Joystick *dev) { (void)dev; return 0; }
int SDL_JoystickNumHats(SDL_Joystick *dev) { (void)dev; return 0; }
int SDL_JoystickNumBalls(SDL_Joystick *dev) { (void)dev; return 0; }
uint8_t SDL_JoystickGetButton(SDL_Joystick *dev, int button) { (void)dev; (void)button; return 0; }
uint8_t SDL_JoystickGetHat(SDL_Joystick *dev, int hat) { (void)dev; (void)hat; return 0; }
int16_t SDL_JoystickGetAxis(SDL_Joystick *dev, int axis) { (void)dev; (void)axis; return 0; }
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *dev) { (void)dev; return 0; }
void SDL_JoystickUpdate(void) {}
int SDL_SetHintWithPriority(const char *name, const char *value, int priority) { (void)name; (void)value; (void)priority; return 0; }

int SDL_Init(uint32_t flags) { (void)flags; return 0; }
int SDL_InitSubSystem(uint32_t flags) { (void)flags; return 0; }
void SDL_Quit(void) {}
void SDL_QuitSubSystem(uint32_t flags) { (void)flags; }
uint32_t SDL_WasInit(uint32_t flags) { (void)flags; return 0; }

uint64_t SDL_GetPerformanceCounter(void)
{
	return (uint64_t)clock();
}

uint64_t SDL_GetPerformanceFrequency(void)
{
	return (uint64_t)CLOCKS_PER_SEC;
}

void SDL_Delay(uint32_t ms)
{
#ifdef __CELLOS_LV2__
	sys_timer_usleep(ms * 1000);
#else
	(void)ms;
#endif
}

int SDL_ShowMessageBox(const SDL_MessageBoxData *messageboxdata, int *buttonid) { (void)messageboxdata; if (buttonid) *buttonid = 0; return 0; }
int SDL_ShowSimpleMessageBox(uint32_t flags, const char *title, const char *message, SDL_Window *window) { (void)flags; (void)title; (void)message; (void)window; return 0; }
int SDL_OpenURL(const char *url) { (void)url; return 0; }
const char *SDL_GetPlatform(void) { return "PlayStation 3"; }

void SDL_StartTextInput(void) {}
void SDL_StopTextInput(void) {}
SDL_bool SDL_IsTextInputActive(void) { return SDL_FALSE; }

int SDL_SetClipboardText(const char *text) { (void)text; return 0; }
SDL_bool SDL_HasClipboardText(void) { return SDL_FALSE; }
char *SDL_GetClipboardText(void) { return NULL; }
void SDL_free(void *mem) { free(mem); }

const char *SDL_GetError(void) { return ""; }

void SDL_GetVersion(SDL_version *ver)
{
	if (ver)
	{
		ver->major = 2;
		ver->minor = 0;
		ver->patch = 0;
	}
}

SDL_bool SDL_HasRDTSC(void) { return SDL_FALSE; }
SDL_bool SDL_HasMMX(void) { return SDL_FALSE; }
SDL_bool SDL_Has3DNow(void) { return SDL_FALSE; }
SDL_bool SDL_HasSSE(void) { return SDL_FALSE; }
SDL_bool SDL_HasSSE2(void) { return SDL_FALSE; }
SDL_bool SDL_HasAltiVec(void) { return SDL_TRUE; } // Cell PPE has AltiVec!
