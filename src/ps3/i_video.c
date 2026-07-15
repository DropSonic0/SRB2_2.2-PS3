#include "../doomdef.h"
#include "../command.h"
#include "../i_video.h"
#include "../screen.h"
#include "../v_video.h"

#ifdef __CELLOS_LV2__
#include <PSGL/psgl.h>
#include <PSGL/psglu.h>
#include <stdlib.h>
#include <string.h>
#endif

rendermode_t rendermode = render_none;
rendermode_t chosenrendermode = render_none;

boolean allow_fullscreen = false;

consvar_t cv_vidwait = CVAR_INIT("vid_wait", "On", CV_SAVE, CV_OnOff, NULL);

#ifdef __CELLOS_LV2__
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif

#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif

#ifndef GL_UNSIGNED_INT_8_8_8_8_REV
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367
#endif

static PSGLdevice* psgl_device = NULL;
static PSGLcontext* psgl_context = NULL;
static GLuint screen_texture = 0;
static UINT32 *gl_buffer = NULL;
static RGBA_t current_palette[256];
static GLuint device_width = 0;
static GLuint device_height = 0;
extern UINT8 graphics_started;
#endif

static INT32 windowedModes[1][2] = {
	{ 640, 400 }
};

void I_StartupGraphics(void)
{
#ifdef __CELLOS_LV2__
	PSGLinitOptions options;
	PSGLdeviceParameters params;

	memset(&options, 0, sizeof(options));
	options.enable = PSGL_INIT_MAX_SPUS | PSGL_INIT_INITIALIZE_SPUS | PSGL_INIT_HOST_MEMORY_SIZE;
	options.maxSPUs = 1;
	options.initializeSPUs = true;
	options.persistentMemorySize = 0;
	options.transientMemorySize = 0;
	options.errorConsole = 0;
	options.fifoSize = 0;
	options.hostMemorySize = 64 * 1024 * 1024;

	CONS_Printf("I_StartupGraphics: Calling psglInit...\n");
	psglInit(&options);

	memset(&params, 0, sizeof(params));
	params.enable = PSGL_DEVICE_PARAMETERS_COLOR_FORMAT | PSGL_DEVICE_PARAMETERS_DEPTH_FORMAT | PSGL_DEVICE_PARAMETERS_MULTISAMPLING_MODE | PSGL_DEVICE_PARAMETERS_BUFFERING_MODE | PSGL_DEVICE_PARAMETERS_RESC_ADJUST_ASPECT_RATIO;
	params.bufferingMode = PSGL_BUFFERING_MODE_TRIPLE;
	params.colorFormat = GL_ARGB_SCE;
	params.depthFormat = GL_NONE;
	params.multisamplingMode = GL_MULTISAMPLING_NONE_SCE;
	params.enable |= PSGL_DEVICE_PARAMETERS_RESC_RATIO_MODE;
	params.rescRatioMode = RESC_RATIO_MODE_FULLSCREEN;

	CONS_Printf("I_StartupGraphics: Calling psglCreateDeviceExtended...\n");
	psgl_device = psglCreateDeviceExtended(&params);
	CONS_Printf("I_StartupGraphics: psgl_device = %p\n", psgl_device);

	CONS_Printf("I_StartupGraphics: Calling psglCreateContext...\n");
	psgl_context = psglCreateContext();
	CONS_Printf("I_StartupGraphics: psgl_context = %p\n", psgl_context);

	CONS_Printf("I_StartupGraphics: Calling psglMakeCurrent...\n");
	psglMakeCurrent(psgl_context, psgl_device);

	GLuint w = 0, h = 0;
	psglGetDeviceDimensions(psgl_device, &w, &h);
	device_width = w;
	device_height = h;
	CONS_Printf("I_StartupGraphics: PS3 Device Size: %ux%u\n", w, h);

	CONS_Printf("I_StartupGraphics: Activating texture unit 0...\n");
	glActiveTexture(GL_TEXTURE0);

	CONS_Printf("I_StartupGraphics: Generating screen texture...\n");
	glGenTextures(1, &screen_texture);
	glBindTexture(GL_TEXTURE_2D, screen_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F); // GL_CLAMP_TO_EDGE
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F); // GL_CLAMP_TO_EDGE
	CONS_Printf("I_StartupGraphics: screen_texture ID = %u\n", screen_texture);

	CONS_Printf("I_StartupGraphics: Initializing 1024x512 POT texture on GPU...\n");
	glTexImage2D(GL_TEXTURE_2D, 0, GL_ARGB_SCE, 1024, 512, 0, GL_ARGB_SCE, GL_UNSIGNED_BYTE, NULL);
	CONS_Printf("I_StartupGraphics: Texture initialized successfully.\n");

	rendermode = render_soft;
	chosenrendermode = render_soft;

	glClearColor(0.2f, 0.3f, 0.4f, 1.0f);

	// Automatically set default resolution mode
	CONS_Printf("I_StartupGraphics: Setting video mode via VID_SetMode(0)...\n");
	VID_SetMode(0);
	CONS_Printf("I_StartupGraphics: Video mode set successfully.\n");
	graphics_started = 1;
#endif
}

void I_ShutdownGraphics(void)
{
#ifdef __CELLOS_LV2__
	graphics_started = 0;
	if (gl_buffer)
	{
		free(gl_buffer);
		gl_buffer = NULL;
	}
	if (vid.buffer)
	{
		free(vid.buffer);
		vid.buffer = NULL;
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
#endif
}

void VID_StartupOpenGL(void){}

void I_SetPalette(RGBA_t *palette)
{
#ifdef __CELLOS_LV2__
	memcpy(current_palette, palette, sizeof(current_palette));
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
	vid.rowbytes = vid.width * vid.bpp;
	vid.modenum = 0;

#ifdef __CELLOS_LV2__
	if (vid.buffer)
	{
		free(vid.buffer);
		vid.buffer = NULL;
	}
	vid.buffer = calloc(NUMSCREENS, vid.rowbytes * vid.height);
	if (vid.buffer)
	{
		int i;
		screens[0] = vid.buffer;
		for (i = 1; i < NUMSCREENS; i++)
		{
			screens[i] = screens[i - 1] + vid.rowbytes * vid.height;
		}
	}

	if (gl_buffer)
	{
		free(gl_buffer);
		gl_buffer = NULL;
	}
	CONS_Printf("VID_SetMode: Allocating 640x400 gl_buffer...\n");
	gl_buffer = calloc(640 * 400, sizeof(UINT32));
	CONS_Printf("VID_SetMode: gl_buffer allocated at %p\n", gl_buffer);
#endif

	return 0;
}

boolean VID_CheckRenderer(void)
{
	return false;
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

	I_OutputMsg("  I_FinishUpdate: Entered.\n");

	if (screens[0] && gl_buffer)
	{
		int i;
		int num_pixels = vid.width * vid.height;

		static boolean palette_checked = false;
		if (!palette_checked)
		{
			palette_checked = true;
			int non_zero_pixels = 0;
			int k;
			for (k = 0; k < num_pixels; k++)
			{
				if (screens[0][k] != 0)
					non_zero_pixels++;
			}
			I_OutputMsg("I_FinishUpdate First Frame: screens[0] has %d non-zero pixels. Palette[1] alpha=%u, r=%u, g=%u, b=%u\n",
				non_zero_pixels, current_palette[1].s.alpha, current_palette[1].s.red, current_palette[1].s.green, current_palette[1].s.blue);
		}

		I_OutputMsg("  I_FinishUpdate: Converting palette...\n");
		for (i = 0; i < num_pixels; i++)
		{
			UINT8 index = screens[0][i];
			RGBA_t color = current_palette[index];
			// If the palette is not set yet (alpha == 0), fallback to grayscale to prevent black screen
			if (color.s.alpha == 0)
			{
				gl_buffer[i] = (0xFF << 24) | (index << 16) | (index << 8) | index;
			}
			else
			{
				// Convert to ARGB (GL_ARGB_SCE format as used by PSGL)
				gl_buffer[i] = (color.s.alpha << 24) | (color.s.red << 16) | (color.s.green << 8) | color.s.blue;
			}
		}

		I_OutputMsg("  I_FinishUpdate: Disabling texture coordinate arrays on other units...\n");
		// Disable all texture coordinate arrays and texturing on other texture units (1 to 7) to prevent driver crashes
		int u;
		for (u = 1; u < 8; u++)
		{
			glActiveTexture(GL_TEXTURE0 + u);
			glDisable(GL_TEXTURE_2D);
			glClientActiveTexture(GL_TEXTURE0 + u);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		}

		I_OutputMsg("  I_FinishUpdate: Activating and binding texture unit 0...\n");
		// Activate and bind texture unit 0
		glActiveTexture(GL_TEXTURE0);
		glClientActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, screen_texture);

		I_OutputMsg("  I_FinishUpdate: Uploading buffer via glTexSubImage2D...\n");
		// Upload the 640x400 buffer into the 1024x512 texture using glTexSubImage2D
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vid.width, vid.height, GL_ARGB_SCE, GL_UNSIGNED_BYTE, gl_buffer);

		I_OutputMsg("  I_FinishUpdate: Disabling 3D states...\n");
		// Disable 3D states that might distort or prevent rendering
		glDisable(GL_LIGHTING);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);
		glDisable(GL_BLEND);

		I_OutputMsg("  I_FinishUpdate: Setting viewport...\n");
		glViewport(0, 0, device_width, device_height);

		I_OutputMsg("  I_FinishUpdate: Setting texture env and color...\n");
		// Set texture environment and color
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

		I_OutputMsg("  I_FinishUpdate: Loading projection matrices...\n");
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrthof(0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();

		I_OutputMsg("  I_FinishUpdate: Clearing color buffer...\n");
		glClear(GL_COLOR_BUFFER_BIT);

		I_OutputMsg("  I_FinishUpdate: Enabling 2D texturing...\n");
		glEnable(GL_TEXTURE_2D);

		I_OutputMsg("  I_FinishUpdate: Configuring client states...\n");
		glDisableClientState(GL_COLOR_ARRAY);
		glDisableClientState(GL_NORMAL_ARRAY);
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);

		static GLfloat vertices[] __attribute__((aligned(16))) = {
			0.0f, 0.0f, 0.0f, // Triangle 1: Bottom-Left
			1.0f, 0.0f, 0.0f, // Triangle 1: Bottom-Right
			0.0f, 1.0f, 0.0f, // Triangle 1: Top-Left

			0.0f, 1.0f, 0.0f, // Triangle 2: Top-Left
			1.0f, 0.0f, 0.0f, // Triangle 2: Bottom-Right
			1.0f, 1.0f, 0.0f  // Triangle 2: Top-Right
		};
		static GLfloat texcoords[] __attribute__((aligned(16))) = {
			0.0f, 0.78125f,   // Triangle 1: Bottom-Left
			0.625f, 0.78125f, // Triangle 1: Bottom-Right
			0.0f, 0.0f,       // Triangle 1: Top-Left

			0.0f, 0.0f,       // Triangle 2: Top-Left
			0.625f, 0.78125f, // Triangle 2: Bottom-Right
			0.625f, 0.0f      // Triangle 2: Top-Right
		};

		I_OutputMsg("  I_FinishUpdate: Setting vertex/texture pointers...\n");
		glVertexPointer(3, GL_FLOAT, 0, vertices);
		glTexCoordPointer(2, GL_FLOAT, 0, texcoords);

		I_OutputMsg("  I_FinishUpdate: Drawing arrays...\n");
		glDrawArrays(GL_TRIANGLES, 0, 6);

		I_OutputMsg("  I_FinishUpdate: Disabling client states...\n");
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glDisableClientState(GL_VERTEX_ARRAY);

		glDisable(GL_TEXTURE_2D);

		I_OutputMsg("  I_FinishUpdate: Flushing OpenGL...\n");
		glFlush();

		I_OutputMsg("  I_FinishUpdate: Swapping buffers via psglSwap()...\n");
		psglSwap();
		I_OutputMsg("  I_FinishUpdate: Swapping completed successfully.\n");
	}
	else
	{
		I_OutputMsg("  I_FinishUpdate: Skip (screens[0]=%p, gl_buffer=%p).\n", screens[0], gl_buffer);
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
	(void)scr;
}

void I_BeginRead(void){}

void I_EndRead(void){}
