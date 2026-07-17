#include "../doomdef.h"
#include "../command.h"
#include "../i_video.h"
#include "../screen.h"
#include "../v_video.h"
#include "../i_system.h"

#ifdef HWRENDER
#include "../hardware/hw_main.h"
#endif

#ifdef __CELLOS_LV2__
#include <PSGL/psgl.h>
#include <PSGL/psglu.h>
#include <stdlib.h>
#include <string.h>

extern void *memalign(size_t boundary, size_t size);

INT32 oglflags = 0;

boolean LoadGL(void)
{
	return true;
}

void *GetGLFunc(const char *name)
{
	(void)name;
	return NULL;
}
#endif

rendermode_t rendermode = render_none;
rendermode_t chosenrendermode = render_none;

boolean allow_fullscreen = false;

consvar_t cv_vidwait = CVAR_INIT("vid_wait", "On", CV_SAVE, CV_OnOff, NULL);

#ifdef __CELLOS_LV2__
static PSGLdevice* psgl_device = NULL;
static PSGLcontext* psgl_context = NULL;
#define NUM_SCREEN_TEXS 3
static GLuint screen_textures[NUM_SCREEN_TEXS] = { 0 };
static GLuint vbo_retro[NUM_SCREEN_TEXS] = { 0 };
static int current_tex_idx = 0;
static UINT32 *gl_buffer = NULL;
static RGBA_t current_palette[256];
static UINT32 gl_palette[256];
#endif

static INT32 windowedModes[1][2] = {
	{ 640, 400 }
};

void I_StartupGraphics(void)
{
#ifdef __CELLOS_LV2__
	PSGLinitOptions options;
	PSGLdeviceParameters params;

	// Guarantee vid.buffer is NULL on startup to prevent freeing uninitialized memory
	vid.buffer = NULL;

	memset(&options, 0, sizeof(options));
	options.enable = PSGL_INIT_MAX_SPUS | PSGL_INIT_INITIALIZE_SPUS | PSGL_INIT_HOST_MEMORY_SIZE;
	options.maxSPUs = 2;
	options.initializeSPUs = true;
	options.persistentMemorySize = 0;
	options.transientMemorySize = 0;
	options.errorConsole = 0;
	options.fifoSize = 0;
	options.hostMemorySize = 16 * 1024 * 1024;

	psglInit(&options);

	memset(&params, 0, sizeof(params));
	params.enable = PSGL_DEVICE_PARAMETERS_COLOR_FORMAT | PSGL_DEVICE_PARAMETERS_DEPTH_FORMAT | PSGL_DEVICE_PARAMETERS_MULTISAMPLING_MODE | PSGL_DEVICE_PARAMETERS_BUFFERING_MODE | PSGL_DEVICE_PARAMETERS_RESC_ADJUST_ASPECT_RATIO;
	params.bufferingMode = PSGL_BUFFERING_MODE_TRIPLE;
	params.colorFormat = GL_ARGB_SCE;
	params.depthFormat = GL_NONE;
	params.multisamplingMode = GL_MULTISAMPLING_NONE_SCE;
	params.enable |= PSGL_DEVICE_PARAMETERS_RESC_RATIO_MODE;
	params.rescRatioMode = RESC_RATIO_MODE_FULLSCREEN;

	psgl_device = psglCreateDeviceExtended(&params);
	psgl_context = psglCreateContext();
	psglMakeCurrent(psgl_context, psgl_device);

	glGenTextures(NUM_SCREEN_TEXS, screen_textures);
	glGenBuffers(NUM_SCREEN_TEXS, vbo_retro);
	{
		int t;
		for (t = 0; t < NUM_SCREEN_TEXS; t++)
		{
			glBindTexture(GL_TEXTURE_2D, screen_textures[t]);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

			glBindBuffer(GL_TEXTURE_REFERENCE_BUFFER_SCE, vbo_retro[t]);
			glBufferData(GL_TEXTURE_REFERENCE_BUFFER_SCE, 1024 * 1024 * sizeof(UINT32), NULL, GL_STREAM_DRAW);
		}
		glBindBuffer(GL_TEXTURE_REFERENCE_BUFFER_SCE, 0);
	}
	current_tex_idx = 0;

#ifdef HWRENDER
	if (M_CheckParm("-opengl"))
	{
		rendermode = render_opengl;
		chosenrendermode = render_opengl;
		CONS_Printf("[PS3] Initializing in Hardware OpenGL/PSGL mode (-opengl parameter detected)\n");
	}
	else
#endif
	{
		rendermode = render_soft;
		chosenrendermode = render_soft;
		CONS_Printf("[PS3] Initializing in Software mode (default)\n");
	}

	// Automatically set default resolution mode
	VID_SetMode(0);
#endif
}

void I_ShutdownGraphics(void)
{
#ifdef __CELLOS_LV2__
	if (gl_buffer)
	{
		free(gl_buffer);
		gl_buffer = NULL;
	}
	if (psgl_context)
	{
		psglDestroyContext(psgl_context);
		psgl_context = NULL;
	}
	if (psgl_device)
	{
		psglDestroyDevice(psgl_device);
		psgl_device = NULL;
	}
	if (vid.buffer)
	{
		free(vid.buffer);
		vid.buffer = NULL;
	}
	glDeleteBuffers(NUM_SCREEN_TEXS, vbo_retro);
#endif
}

void VID_StartupOpenGL(void){}

void I_SetPalette(RGBA_t *palette)
{
#ifdef __CELLOS_LV2__
	int i;
	memcpy(current_palette, palette, sizeof(current_palette));
	for (i = 0; i < 256; i++)
	{
		RGBA_t color = current_palette[i];
		// Pack as ARGB for GL_ARGB_SCE on Big Endian PS3 so that colors map properly on screen
		gl_palette[i] = (color.s.alpha << 24) | (color.s.red << 16) | (color.s.green << 8) | color.s.blue;
	}
#else
	(void)palette;
#endif
}

INT32 VID_NumModes(void)
{
	return 1;
}

INT32 VID_GetModeForSize(INT32 w, INT32 h)
{
	(void)w;
	(void)h;
	return 0;
}

void VID_PrepareModeList(void){}

INT32 VID_SetMode(INT32 modenum)
{
	if (modenum < 0)
		modenum = 0;

	vid.recalc = 1;
	vid.bpp = 1;
	vid.width = windowedModes[0][0];
	vid.height = windowedModes[0][1];
	vid.modenum = 0;

	// Set up game's software render buffer
	vid.rowbytes = vid.width * vid.bpp;
	vid.direct = NULL;
	if (vid.buffer)
	{
		free(vid.buffer);
		vid.buffer = NULL;
	}
	vid.buffer = calloc(NUMSCREENS, vid.rowbytes * vid.height);
	if (!vid.buffer)
	{
		printf("Error: Not enough memory for video buffer\n");
	}

	return 0;
}

boolean VID_CheckRenderer(void)
{
	boolean rendererchanged = false;
	if (setrenderneeded)
	{
		rendermode = setrenderneeded;
		rendererchanged = true;
		setrenderneeded = 0;
	}

	if (rendermode == render_soft)
	{
		SCR_SetDrawFuncs();
	}
#ifdef HWRENDER
	else if (rendermode == render_opengl && rendererchanged)
	{
		HWR_Switch();
		V_SetPalette(0);
	}
#endif

	return rendererchanged;
}

void VID_CheckGLLoaded(rendermode_t oldrender)
{
	(void)oldrender;
}

const char *VID_GetModeName(INT32 modenum)
{
	(void)modenum;
	return "640x400";
}

UINT32 I_GetRefreshRate(void) { return 35; }

void I_UpdateNoBlit(void){}

void I_FinishUpdate(void)
{
#ifdef __CELLOS_LV2__
	if (rendermode == render_none)
		return;

	if (rendermode == render_soft)
	{
		if (screens[0] && vid.width > 0 && vid.height > 0)
		{
			static int gl_buffer_w = 0;
			static int gl_buffer_h = 0;

			// Allocate gl_buffer with exact dimensions (640x400)
			if (!gl_buffer || gl_buffer_w != (int)vid.width || gl_buffer_h != (int)vid.height)
			{
				if (gl_buffer)
				{
					free(gl_buffer);
				}
				gl_buffer = memalign(128, vid.width * vid.height * sizeof(UINT32));
				gl_buffer_w = vid.width;
				gl_buffer_h = vid.height;
			}

			if (gl_buffer)
			{
				int num_pixels = vid.width * vid.height;
				int i;
				// Unrolled pixel conversion loop by 8 for maximum performance on PS3 PPU
				for (i = 0; i < num_pixels; i += 8)
				{
					gl_buffer[i] = gl_palette[screens[0][i]];
					gl_buffer[i + 1] = gl_palette[screens[0][i + 1]];
					gl_buffer[i + 2] = gl_palette[screens[0][i + 2]];
					gl_buffer[i + 3] = gl_palette[screens[0][i + 3]];
					gl_buffer[i + 4] = gl_palette[screens[0][i + 4]];
					gl_buffer[i + 5] = gl_palette[screens[0][i + 5]];
					gl_buffer[i + 6] = gl_palette[screens[0][i + 6]];
					gl_buffer[i + 7] = gl_palette[screens[0][i + 7]];
				}

				// Rotate to next screen texture for triple-buffered uploads (prevents GPU pipeline stall)
				current_tex_idx = (current_tex_idx + 1) % NUM_SCREEN_TEXS;

				// Use zero-copy texture references in fast-path VBO memory
				glBindBuffer(GL_TEXTURE_REFERENCE_BUFFER_SCE, vbo_retro[current_tex_idx]);
				glBufferSubData(GL_TEXTURE_REFERENCE_BUFFER_SCE, 0, vid.width * vid.height * sizeof(UINT32), gl_buffer);

				glBindTexture(GL_TEXTURE_2D, screen_textures[current_tex_idx]);
				glTextureReferenceSCE(GL_TEXTURE_2D, 1, vid.width, vid.height, 0, GL_ARGB_SCE, vid.width * sizeof(UINT32), 0);
				glBindBuffer(GL_TEXTURE_REFERENCE_BUFFER_SCE, 0);

				glMatrixMode(GL_PROJECTION);
				glLoadIdentity();
				glOrthof(0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f);
				glMatrixMode(GL_MODELVIEW);
				glLoadIdentity();

				glClear(GL_COLOR_BUFFER_BIT);
				glEnable(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, screen_textures[current_tex_idx]);

				GLfloat vertices[] = {
					0.0f, 0.0f,
					1.0f, 0.0f,
					1.0f, 1.0f,
					0.0f, 1.0f
				};
				GLfloat texcoords[] = {
					0.0f, 1.0f,
					1.0f, 1.0f,
					1.0f, 0.0f,
					0.0f, 0.0f
				};

				glEnableClientState(GL_VERTEX_ARRAY);
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);

				glVertexPointer(2, GL_FLOAT, 0, vertices);
				glTexCoordPointer(2, GL_FLOAT, 0, texcoords);

				glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

				glDisableClientState(GL_TEXTURE_COORD_ARRAY);
				glDisableClientState(GL_VERTEX_ARRAY);

				glDisable(GL_TEXTURE_2D);

				psglSwap();
			}
		}
	}
	else if (rendermode == render_opengl)
	{
		psglSwap();
	}
#endif
}

void I_UpdateNoVsync(void) {}

void I_WaitVBL(INT32 count)
{
	(void)count;
}

void I_ReadScreen(UINT8 *scr)
{
	if (rendermode != render_soft)
		I_Error("I_ReadScreen: called while in non-software mode");
	else
		VID_BlitLinearScreen(screens[0], scr,
		vid.width*vid.bpp, vid.height,
		vid.rowbytes, vid.rowbytes);
}

void I_BeginRead(void){}

void I_EndRead(void){}