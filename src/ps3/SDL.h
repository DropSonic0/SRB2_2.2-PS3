#ifndef PS3_SDL_H
#define PS3_SDL_H

#include <stdint.h>
#include <stddef.h>

typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef uint64_t Uint64;
typedef int8_t Sint8;
typedef int16_t Sint16;
typedef int32_t Sint32;
typedef int64_t Sint64;

typedef enum {
    SDL_FALSE = 0,
    SDL_TRUE = 1
} SDL_bool;

typedef struct SDL_Joystick SDL_Joystick;
typedef struct SDL_Window SDL_Window;
typedef uint32_t SDL_JoystickID;

typedef struct {
    int major;
    int minor;
    int patch;
} SDL_version;

typedef struct {
    uint32_t flags;
    const void *window;
    const char *title;
    const char *message;
    int numbuttons;
    const void *buttons;
    const void *colorScheme;
} SDL_MessageBoxData;

typedef struct {
    uint32_t flags;
    int id;
    const char *text;
} SDL_MessageBoxButtonData;

#define SDL_MESSAGEBOX_ERROR 0
#define SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT 0
#define SDL_arraysize(array) (sizeof(array)/sizeof(*(array)))

#define SDL_INIT_AUDIO 0x00000010
#define SDL_INIT_VIDEO 0x00000020
#define SDL_INIT_JOYSTICK 0x00000200

#define SDL_HAT_UP 0x01
#define SDL_HAT_DOWN 0x02
#define SDL_HAT_LEFT 0x04
#define SDL_HAT_RIGHT 0x08

#define SDL_VERSION(v) { (v)->major = 2; (v)->minor = 0; (v)->patch = 0; }

// Function declarations for stubs
void SDL_GetVersion(SDL_version *ver);
void SDL_JoystickClose(SDL_Joystick *dev);
int SDL_NumJoysticks(void);
SDL_Joystick *SDL_JoystickOpen(int device_index);
SDL_bool SDL_JoystickGetAttached(SDL_Joystick *dev);
const char *SDL_JoystickName(SDL_Joystick *dev);
const char *SDL_JoystickNameForIndex(int device_index);
int SDL_JoystickNumAxes(SDL_Joystick *dev);
int SDL_JoystickNumButtons(SDL_Joystick *dev);
int SDL_JoystickNumHats(SDL_Joystick *dev);
int SDL_JoystickNumBalls(SDL_Joystick *dev);
uint8_t SDL_JoystickGetButton(SDL_Joystick *dev, int button);
uint8_t SDL_JoystickGetHat(SDL_Joystick *dev, int hat);
int16_t SDL_JoystickGetAxis(SDL_Joystick *dev, int axis);
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *dev);
void SDL_JoystickUpdate(void);
int SDL_SetHintWithPriority(const char *name, const char *value, int priority);
#define SDL_HINT_OVERRIDE 0

int SDL_Init(uint32_t flags);
int SDL_InitSubSystem(uint32_t flags);
void SDL_Quit(void);
void SDL_QuitSubSystem(uint32_t flags);
uint32_t SDL_WasInit(uint32_t flags);

uint64_t SDL_GetPerformanceCounter(void);
uint64_t SDL_GetPerformanceFrequency(void);
void SDL_Delay(uint32_t ms);

int SDL_ShowMessageBox(const SDL_MessageBoxData *messageboxdata, int *buttonid);
int SDL_ShowSimpleMessageBox(uint32_t flags, const char *title, const char *message, SDL_Window *window);
int SDL_OpenURL(const char *url);
const char *SDL_GetPlatform(void);

void SDL_StartTextInput(void);
void SDL_StopTextInput(void);
SDL_bool SDL_IsTextInputActive(void);

int SDL_SetClipboardText(const char *text);
SDL_bool SDL_HasClipboardText(void);
char *SDL_GetClipboardText(void);
void SDL_free(void *mem);

const char *SDL_GetError(void);

#define SDL_VERSION_ATLEAST(X, Y, Z) \
    (1)

#endif
