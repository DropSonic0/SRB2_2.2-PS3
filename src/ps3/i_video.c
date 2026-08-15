#include "../doomdef.h"
#include "../command.h"
#include "../i_video.h"
#include "../screen.h"
#include "../v_video.h"
#include "../i_system.h"
#include "../doomstat.h"
#include "../s_sound.h"
#include "../netcode/d_netcmd.h"

#ifdef HWRENDER
#include "../hardware/hw_drv.h"
#include "../hardware/hw_main.h"
#include "../hardware/r_opengl/r_opengl.h"

extern void *PS3_GetGLSym(const char *name);

void *hwSym(const char *funcName, void *handle)
{
	(void)handle;
	return PS3_GetGLSym(funcName);
}
#endif

#ifdef __CELLOS_LV2__
#include <PSGL/psgl.h>
#include <PSGL/psglu.h>
#include <PSGL/report.h>
#include <sysutil/sysutil_sysparam.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void PS3_DebugLog(const char *msg)
{
	FILE *f = fopen("/dev_hdd0/game/SRB222PS3/USRDIR/log.txt", "a");
	if (f)
	{
		fputs(msg, f);
		fflush(f);
		fclose(f);
	}
}
#define PS3_DBG(msg) PS3_DebugLog(msg)

static void PS3_PSGLReportCallback(GLenum report, GLuint id, const char *message)
{
	char buf[512];
	snprintf(buf, sizeof(buf), "[PSGL_REPORT] enum=0x%x id=%u msg=%s\n",
		(unsigned int)report, (unsigned int)id, message ? message : "(null)");
	PS3_DebugLog(buf);
}

extern void *memalign(size_t boundary, size_t size);

INT32 oglflags = 0;
UINT32 display_width = 1280;
UINT32 display_height = 720;

boolean LoadGL(void)
{
	return true;
}

static void PS3_glMultiTexCoord2f(GLenum target, GLfloat s, GLfloat t)
{
	(void)target; (void)s; (void)t;
}

static void PS3_glMultiTexCoord2fv(GLenum target, const GLfloat *v)
{
	(void)target; (void)v;
}

static void PS3_glColor4ubv(const GLubyte *v)
{
	if (v)
		glColor4ub(v[0], v[1], v[2], v[3]);
}

static void PS3_glMateriali(GLenum face, GLenum pname, GLint param)
{
	glMaterialf(face, pname, (GLfloat)param);
}

static void PS3_glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
	(void)target; (void)level; (void)internalformat; (void)width; (void)border; (void)format; (void)type; (void)pixels;
}

static void PS3_glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels)
{
	(void)target; (void)level; (void)format; (void)type; (void)pixels;
}

void *GetGLFunc(const char *name)
{
	if (!name) return NULL;

#define MAP_GL(func) if (strcmp(name, #func) == 0) return (void*)(func)

	MAP_GL(glClearColor);
	MAP_GL(glClear);
	MAP_GL(glColorMask);
	MAP_GL(glAlphaFunc);
	MAP_GL(glBlendFunc);
	MAP_GL(glCullFace);
	MAP_GL(glPolygonOffset);
	MAP_GL(glScissor);
	MAP_GL(glEnable);
	MAP_GL(glDisable);
	MAP_GL(glGetFloatv);
	MAP_GL(glGetIntegerv);
	MAP_GL(glGetString);
	MAP_GL(glPolygonMode);
	MAP_GL(glClearDepth);
	MAP_GL(glDepthFunc);
	MAP_GL(glDepthMask);
	MAP_GL(glDepthRange);
	MAP_GL(glMatrixMode);
	MAP_GL(glViewport);
	MAP_GL(glPushMatrix);
	MAP_GL(glPopMatrix);
	MAP_GL(glLoadIdentity);
	MAP_GL(glMultMatrixf);
	MAP_GL(glRotatef);
	MAP_GL(glScalef);
	MAP_GL(glTranslatef);
	MAP_GL(glVertexPointer);
	MAP_GL(glNormalPointer);
	MAP_GL(glTexCoordPointer);
	MAP_GL(glColorPointer);
	MAP_GL(glDrawArrays);
	MAP_GL(glDrawElements);
	MAP_GL(glEnableClientState);
	MAP_GL(glDisableClientState);
	MAP_GL(glShadeModel);
	MAP_GL(glLightfv);
	MAP_GL(glLightModelfv);
	MAP_GL(glMaterialfv);
	MAP_GL(glPixelStorei);
	MAP_GL(glReadPixels);
	MAP_GL(glTexEnvi);
	MAP_GL(glTexParameteri);
	MAP_GL(glTexImage2D);
	MAP_GL(glTexSubImage2D);
	MAP_GL(glGenTextures);
	MAP_GL(glDeleteTextures);
	MAP_GL(glBindTexture);
	MAP_GL(glCopyTexImage2D);
	MAP_GL(glCopyTexSubImage2D);
	MAP_GL(glActiveTexture);
	MAP_GL(glClientActiveTexture);
	MAP_GL(glGenBuffers);
	MAP_GL(glBindBuffer);
	MAP_GL(glBufferData);
	MAP_GL(glDeleteBuffers);
	MAP_GL(glBlendEquation);

#undef MAP_GL

	if (strcmp(name, "glMultiTexCoord2f") == 0) return (void*)PS3_glMultiTexCoord2f;
	if (strcmp(name, "glMultiTexCoord2fv") == 0) return (void*)PS3_glMultiTexCoord2fv;
	if (strcmp(name, "glColor4ubv") == 0) return (void*)PS3_glColor4ubv;
	if (strcmp(name, "glMateriali") == 0) return (void*)PS3_glMateriali;
	if (strcmp(name, "glTexImage1D") == 0) return (void*)PS3_glTexImage1D;
	if (strcmp(name, "glGetTexImage") == 0) return (void*)PS3_glGetTexImage;

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

void VID_StartupOpenGL(void)
{
#ifdef HWRENDER
	static boolean glstartup = false;
	if (!glstartup)
	{
		CONS_Printf("[PS3] VID_StartupOpenGL()...\n");
		HWD.pfnInit = hwSym("Init", NULL);
		HWD.pfnFinishUpdate = NULL;
		HWD.pfnDraw2DLine = hwSym("Draw2DLine", NULL);
		HWD.pfnDrawPolygon = hwSym("DrawPolygon", NULL);
		HWD.pfnDrawIndexedTriangles = hwSym("DrawIndexedTriangles", NULL);
		HWD.pfnRenderSkyDome = hwSym("RenderSkyDome", NULL);
		HWD.pfnSetBlend = hwSym("SetBlend", NULL);
		HWD.pfnClearBuffer = hwSym("ClearBuffer", NULL);
		HWD.pfnSetTexture = hwSym("SetTexture", NULL);
		HWD.pfnUpdateTexture = hwSym("UpdateTexture", NULL);
		HWD.pfnDeleteTexture = hwSym("DeleteTexture", NULL);
		HWD.pfnReadScreenTexture = hwSym("ReadScreenTexture", NULL);
		HWD.pfnGClipRect = hwSym("GClipRect", NULL);
		HWD.pfnClearMipMapCache = hwSym("ClearMipMapCache", NULL);
		HWD.pfnSetSpecialState = hwSym("SetSpecialState", NULL);
		HWD.pfnSetTexturePalette = hwSym("SetTexturePalette", NULL);
		HWD.pfnGetTextureUsed = hwSym("GetTextureUsed", NULL);
		HWD.pfnDrawModel = hwSym("DrawModel", NULL);
		HWD.pfnCreateModelVBOs = hwSym("CreateModelVBOs", NULL);
		HWD.pfnSetTransform = hwSym("SetTransform", NULL);
		HWD.pfnPostImgRedraw = hwSym("PostImgRedraw", NULL);
		HWD.pfnFlushScreenTextures = hwSym("FlushScreenTextures", NULL);
		HWD.pfnDoScreenWipe = hwSym("DoScreenWipe", NULL);
		HWD.pfnDrawScreenTexture = hwSym("DrawScreenTexture", NULL);
		HWD.pfnMakeScreenTexture = hwSym("MakeScreenTexture", NULL);
		HWD.pfnDrawScreenFinalTexture = hwSym("DrawScreenFinalTexture", NULL);

		HWD.pfnInitShaders = hwSym("InitShaders", NULL);
		HWD.pfnLoadShader = hwSym("LoadShader", NULL);
		HWD.pfnCompileShader = hwSym("CompileShader", NULL);
		HWD.pfnSetShader = hwSym("SetShader", NULL);
		HWD.pfnUnSetShader = hwSym("UnSetShader", NULL);

		HWD.pfnSetShaderInfo = hwSym("SetShaderInfo", NULL);

		HWD.pfnSetPaletteLookup = hwSym("SetPaletteLookup", NULL);
		HWD.pfnCreateLightTable = hwSym("CreateLightTable", NULL);
		HWD.pfnUpdateLightTable = hwSym("UpdateLightTable", NULL);
		HWD.pfnClearLightTables = hwSym("ClearLightTables", NULL);
		HWD.pfnSetScreenPalette = hwSym("SetScreenPalette", NULL);

		vid.glstate = (HWD.pfnInit && HWD.pfnInit()) ? VID_GL_LIBRARY_LOADED : VID_GL_LIBRARY_ERROR;

		if (vid.glstate == VID_GL_LIBRARY_ERROR)
		{
			CONS_Printf("[PS3] Hardware OpenGL/PSGL init failed! Falling back to Software mode.\n");
			rendermode = render_soft;
			chosenrendermode = render_soft;
			setrenderneeded = 0;
		}
		else
		{
			CONS_Printf("[PS3] Hardware OpenGL/PSGL init succeeded! vid.glstate = VID_GL_LIBRARY_LOADED\n");
		}
		glstartup = true;
	}
#endif
}

void I_StartupGraphics(void)
{
#ifdef __CELLOS_LV2__
	PSGLinitOptions options;
	PSGLdeviceParameters params;

	// Guarantee vid.buffer is NULL on startup to prevent freeing uninitialized memory
	vid.buffer = NULL;

	CellVideoOutState videoState;
	if (cellVideoOutGetState(CELL_VIDEO_OUT_PRIMARY, 0, &videoState) == 0)
	{
		CellVideoOutResolution videoResolution;
		if (cellVideoOutGetResolution(videoState.displayMode.resolutionId, &videoResolution) == 0)
		{
			display_width = videoResolution.width;
			display_height = videoResolution.height;
		}
	}
	if (display_width <= 0) display_width = 1280;
	if (display_height <= 0) display_height = 720;

	memset(&options, 0, sizeof(PSGLinitOptions));
	options.enable = PSGL_INIT_MAX_SPUS | PSGL_INIT_INITIALIZE_SPUS | PSGL_INIT_PERSISTENT_MEMORY_SIZE | PSGL_INIT_TRANSIENT_MEMORY_SIZE | PSGL_INIT_FIFO_SIZE | PSGL_INIT_HOST_MEMORY_SIZE;
	options.maxSPUs = 1;
	options.initializeSPUs = true;
	// Previously 1MB/1MB: too small to hold even a single 1024x1024 RGBA
	// screen texture (4MB) from MakeScreenTexture(). PSGL likely defers the
	// real upload of texture data into these pools until the texture is
	// actually used by a draw call, so an undersized pool here can manifest
	// as a hang on the first draw referencing a texture, rather than a clean
	// allocation error at TexImage2D time.
	options.persistentMemorySize = 16 * 1024 * 1024;
	options.transientMemorySize = 16 * 1024 * 1024;
	options.errorConsole = 0;
	options.fifoSize = 2 * 1024 * 1024;
	options.hostMemorySize = 16 * 1024 * 1024;

	psglInit(&options);

	// Hook PSGL's own internal diagnostics (texture/framebuffer completeness,
	// GPU memory allocation failures, Cg errors, etc.) into our debug log,
	// instead of guessing blindly from the outside.
	psglSetReportFunction(PS3_PSGLReportCallback);
	psglEnableReport(PSGL_REPORT_ALL);
	PS3_DBG("I_StartupGraphics: PSGL report function hooked, PSGL_REPORT_ALL enabled\n");

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
	if (M_CheckParm("-software"))
	{
		rendermode = render_soft;
		chosenrendermode = render_soft;
		CONS_Printf("[PS3] Initializing in Software mode (-software parameter detected)\n");
	}
	else
	{
		rendermode = render_opengl;
		chosenrendermode = render_opengl;
		CONS_Printf("[PS3] Initializing in Hardware OpenGL/PSGL mode (default)\n");
	}
#else
	rendermode = render_soft;
	chosenrendermode = render_soft;
	CONS_Printf("[PS3] Initializing in Software mode\n");
#endif

	CV_RegisterVar(&cv_vidwait);

	if (rendermode == render_opengl)
	{
		VID_StartupOpenGL();
	}

	// Automatically set default resolution mode
	VID_SetMode(0);

#ifdef HWRENDER
	// VID_CheckRenderer() only runs HWR_Switch()/V_SetPalette() when the
	// renderer changes at runtime (setrenderneeded). Since we boot directly
	// into render_opengl by default, that path is never hit on first launch,
	// so the hardware renderer's palette/lighttables never get set up.
	if (rendermode == render_opengl && vid.glstate == VID_GL_LIBRARY_LOADED)
	{
		CONS_Printf("[PS3][DEBUG] Running HWR_Switch()...\n");
		HWR_Switch();
		CONS_Printf("[PS3][DEBUG] HWR_Switch() done. Running V_SetPalette(0)...\n");
		V_SetPalette(0);
		CONS_Printf("[PS3][DEBUG] V_SetPalette(0) done.\n");
		CONS_Printf("[PS3][DEBUG] pfnMakeScreenTexture=%p pfnDrawScreenFinalTexture=%p pfnSetScreenPalette=%p pfnSetTransform=%p\n",
			(void*)HWD.pfnMakeScreenTexture, (void*)HWD.pfnDrawScreenFinalTexture,
			(void*)HWD.pfnSetScreenPalette, (void*)HWD.pfnSetTransform);
	}
#endif

	graphics_started = true;
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
		if (vbo_retro[0] != 0)
		{
			glDeleteBuffers(NUM_SCREEN_TEXS, vbo_retro);
			memset(vbo_retro, 0, sizeof(vbo_retro));
		}
		if (screen_textures[0] != 0)
		{
			glDeleteTextures(NUM_SCREEN_TEXS, screen_textures);
			memset(screen_textures, 0, sizeof(screen_textures));
		}
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

	graphics_started = false;
#endif
}

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

#ifdef HWRENDER
	if (rendermode == render_opengl && vid.glstate == VID_GL_LIBRARY_LOADED)
	{
		SetModelView(vid.width, vid.height);
		SetStates();
		HWR_Startup();
	}
#endif

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

	SCR_CalculateFPS();

	if (rendermode == render_soft)
	{
		if (screens[0] && vid.width > 0 && vid.height > 0)
		{
			if (marathonmode)
				SCR_DisplayMarathonInfo();

			// draw captions if enabled
			if (cv_closedcaptioning.value)
				SCR_ClosedCaptions();

			if (cv_ticrate.value)
				SCR_DisplayTicRate();

			if (cv_showping.value && netgame && consoleplayer != serverplayer)
				SCR_DisplayLocalPing();

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
		static boolean firstframe = true;
		if (firstframe)
		{
			PS3_DBG("I_FinishUpdate: first opengl frame\n");
		}
		PS3_DBG("I_FinishUpdate: about to call HWR_MakeScreenFinalTexture\n");
		HWR_MakeScreenFinalTexture();
		PS3_DBG("I_FinishUpdate: HWR_MakeScreenFinalTexture returned\n");
		PS3_DBG("I_FinishUpdate: about to call HWR_DrawScreenFinalTexture\n");
		HWR_DrawScreenFinalTexture(display_width, display_height);
		PS3_DBG("I_FinishUpdate: HWR_DrawScreenFinalTexture returned\n");
		PS3_DBG("I_FinishUpdate: about to call psglSwap\n");
		psglSwap();
		PS3_DBG("I_FinishUpdate: psglSwap returned\n");
		firstframe = false;
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