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
#include "../hardware/hw_main.h"
#endif

#ifdef __CELLOS_LV2__
#include <cell/gcm.h>
#include <cell/gcm/gcm_method_data.h>
#include <cell/sysmodule.h>
#include <sysutil/sysutil_sysparam.h>
#include <cell/resc.h>
#include <PSGL/psgl.h>
#include <PSGL/psglu.h>
#include <stdlib.h>
#include <string.h>
#include "../m_argv.h"

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

/* Libgcm variables */
extern CellGcmContextData gCellGcmContext;

#define COMMAND_BUFFER_SIZE (4 * 1024 * 1024)
static void* host_addr = NULL;
static uint8_t* host_free_ptr = NULL;
static uint8_t* vram_free_ptr = NULL;

static void* display_buffer[2] = { NULL, NULL };
static uint32_t display_offset[2] = { 0, 0 };
static uint32_t display_width = 0;
static uint32_t display_height = 0;
static uint32_t display_pitch = 0;
static int current_buffer_idx = 0;

static void* host_alloc(uint32_t size, uint32_t alignment)
{
	uintptr_t ptr = (uintptr_t)host_free_ptr;
	ptr = (ptr + alignment - 1) & ~(alignment - 1);
	host_free_ptr = (uint8_t*)(ptr + size);
	return (void*)ptr;
}

static void* vram_alloc(uint32_t size, uint32_t alignment)
{
	uintptr_t ptr = (uintptr_t)vram_free_ptr;
	ptr = (ptr + alignment - 1) & ~(alignment - 1);
	vram_free_ptr = (uint8_t*)(ptr + size);
	return (void*)ptr;
}

static void convert_palette_1to1(uint32_t* dst, const uint8_t* src, uint32_t num_pixels, const uint32_t* palette)
{
	uint32_t i;
	// Unrolled pixel conversion loop by 8 for maximum performance on PS3 PPU
	for (i = 0; i < num_pixels; i += 8)
	{
		dst[i] = palette[src[i]];
		dst[i + 1] = palette[src[i + 1]];
		dst[i + 2] = palette[src[i + 2]];
		dst[i + 3] = palette[src[i + 3]];
		dst[i + 4] = palette[src[i + 4]];
		dst[i + 5] = palette[src[i + 5]];
		dst[i + 6] = palette[src[i + 6]];
		dst[i + 7] = palette[src[i + 7]];
	}
}
#endif

static INT32 windowedModes[1][2] = {
	{ 640, 400 }
};

void I_StartupGraphics(void)
{
#ifdef __CELLOS_LV2__
	// Guarantee vid.buffer is NULL on startup to prevent freeing uninitialized memory
	vid.buffer = NULL;

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

	if (rendermode == render_soft)
	{
		int32_t ret;

		CONS_Printf("[PS3] Loading GCM & RESC system modules...\n");
		fflush(stdout);
		cellSysmoduleInitialize();
		cellSysmoduleLoadModule(CELL_SYSMODULE_GCM);
		cellSysmoduleLoadModule(CELL_SYSMODULE_RESC);

		CONS_Printf("[PS3] Allocating GCM host command buffer...\n");
		fflush(stdout);
		host_addr = memalign(1024 * 1024, COMMAND_BUFFER_SIZE);
		host_free_ptr = (uint8_t*)host_addr;
		void* gcm_cmd_buffer = host_alloc(256 * 1024, 1024 * 1024);

		CONS_Printf("[PS3] Initializing GCM library (256KB command buffer)...\n");
		fflush(stdout);
		ret = cellGcmInit(256 * 1024, COMMAND_BUFFER_SIZE, host_addr);
		CONS_Printf("[PS3] cellGcmInit returned: %d\n", ret);
		fflush(stdout);

		CONS_Printf("[PS3] Querying GCM configurations...\n");
		fflush(stdout);
		CellGcmConfig gcmConfig;
		cellGcmGetConfiguration(&gcmConfig);
		vram_free_ptr = (uint8_t*)gcmConfig.localAddress;

		CONS_Printf("[PS3] Querying primary video output state...\n");
		fflush(stdout);
		CellVideoOutState videoState;
		ret = cellVideoOutGetState(CELL_VIDEO_OUT_PRIMARY, 0, &videoState);
		CONS_Printf("[PS3] cellVideoOutGetState returned: %d\n", ret);
		fflush(stdout);

		CONS_Printf("[PS3] Querying video output resolution...\n");
		fflush(stdout);
		CellVideoOutResolution resolution;
		ret = cellVideoOutGetResolution(videoState.displayMode.resolutionId, &resolution);
		CONS_Printf("[PS3] cellVideoOutGetResolution returned: %d\n", ret);
		fflush(stdout);
		display_width = resolution.width;
		display_height = resolution.height;
		display_pitch = display_width * sizeof(UINT32);

		if (display_width == 0 || display_height == 0)
		{
			display_width = 1280;
			display_height = 720;
			display_pitch = display_width * sizeof(UINT32);
		}
		CONS_Printf("[PS3] Target Display Resolution: %dx%d\n", display_width, display_height);
		fflush(stdout);

		CONS_Printf("[PS3] Configuring video output to %dx%d (X8R8G8B8)...\n", display_width, display_height);
		fflush(stdout);
		CellVideoOutConfiguration videoConfig;
		memset(&videoConfig, 0, sizeof(videoConfig));
		videoConfig.resolutionId = videoState.displayMode.resolutionId;
		videoConfig.format = CELL_VIDEO_OUT_BUFFER_COLOR_FORMAT_X8R8G8B8;
		videoConfig.aspect = videoState.displayMode.aspect;
		videoConfig.pitch = display_pitch;
		ret = cellVideoOutConfigure(CELL_VIDEO_OUT_PRIMARY, &videoConfig, NULL, 0);
		CONS_Printf("[PS3] cellVideoOutConfigure returned: %d\n", ret);
		fflush(stdout);

		CONS_Printf("[PS3] Resolving RESC buffer mode...\n");
		fflush(stdout);
		CellRescBufferMode bufferMode;
		ret = cellRescVideoOutResolutionId2RescBufferMode(videoState.displayMode.resolutionId, &bufferMode);
		CONS_Printf("[PS3] cellRescVideoOutResolutionId2RescBufferMode returned: %d\n", ret);
		fflush(stdout);

		CONS_Printf("[PS3] Initializing RESC library...\n");
		fflush(stdout);
		CellRescInitConfig initConfig;
		memset(&initConfig, 0, sizeof(initConfig));
		initConfig.size = sizeof(CellRescInitConfig);
		initConfig.resourcePolicy = CELL_RESC_CONSTANT_VRAM;
		initConfig.supportModes = bufferMode;
		initConfig.ratioMode = CELL_RESC_FULLSCREEN;
		initConfig.palTemporalMode = CELL_RESC_PAL_50;
		initConfig.interlaceMode = CELL_RESC_NORMAL_BILINEAR;
		initConfig.flipMode = CELL_RESC_DISPLAY_VSYNC;

		ret = cellRescInit(&initConfig);
		CONS_Printf("[PS3] cellRescInit returned: %d\n", ret);
		fflush(stdout);

		CONS_Printf("[PS3] Querying RESC required buffer sizes...\n");
		fflush(stdout);
		int32_t colorBuffersSize = 0;
		int32_t vertexArraySize = 0;
		int32_t fragmentShaderSize = 0;
		ret = cellRescGetBufferSize(&colorBuffersSize, &vertexArraySize, &fragmentShaderSize);
		CONS_Printf("[PS3] cellRescGetBufferSize returned: %d\n", ret);
		fflush(stdout);

		CONS_Printf("[PS3] Allocating RESC buffer allocations in VRAM...\n");
		fflush(stdout);
		void* colorBuffersPtr = vram_alloc(colorBuffersSize, 1024 * 1024);
		void* vertexArrayPtr = vram_alloc(vertexArraySize, 128);
		void* fragmentShaderPtr = vram_alloc(fragmentShaderSize, 128);

		CONS_Printf("[PS3] Registering RESC buffer addresses...\n");
		fflush(stdout);
		ret = cellRescSetBufferAddress(colorBuffersPtr, vertexArrayPtr, fragmentShaderPtr);
		CONS_Printf("[PS3] cellRescSetBufferAddress returned: %d\n", ret);
		fflush(stdout);

		CONS_Printf("[PS3] Setting RESC destination structures...\n");
		fflush(stdout);
		CellRescDsts dsts;
		memset(&dsts, 0, sizeof(dsts));
		dsts.format = CELL_RESC_SURFACE_A8R8G8B8;
		dsts.pitch = display_pitch;
		dsts.heightAlign = 1;
		ret = cellRescSetDsts(bufferMode, &dsts);
		CONS_Printf("[PS3] cellRescSetDsts returned: %d\n", ret);
		fflush(stdout);

		CONS_Printf("[PS3] Applying RESC display mode configuration...\n");
		fflush(stdout);
		ret = cellRescSetDisplayMode(bufferMode);
		CONS_Printf("[PS3] cellRescSetDisplayMode returned: %d\n", ret);
		fflush(stdout);

		CONS_Printf("[PS3] Allocating and registering dual 640x400 source buffers in GCM mapped VRAM...\n");
		fflush(stdout);
		uint32_t src_pitch = 640 * sizeof(UINT32);
		uint32_t src_size = src_pitch * 400;

		int i;
		for (i = 0; i < 2; i++)
		{
			display_buffer[i] = vram_alloc(src_size, 1024 * 1024);
			cellGcmAddressToOffset(display_buffer[i], &display_offset[i]);

			CellRescSrc src;
			src.format = CELL_GCM_TEXTURE_A8R8G8B8 | CELL_GCM_TEXTURE_LN | CELL_GCM_TEXTURE_NR;
			src.pitch = src_pitch;
			src.width = 640;
			src.height = 400;
			src.offset = display_offset[i];

			CONS_Printf("[PS3] Populating RESC src %d: format=0x%x, pitch=%d, width=%d, height=%d, offset=0x%x\n",
				i, src.format, src.pitch, src.width, src.height, src.offset);
			fflush(stdout);

			ret = cellRescSetSrc(i, &src);
			CONS_Printf("[PS3] cellRescSetSrc(%d) returned: %d\n", i, ret);
			fflush(stdout);
		}

		current_buffer_idx = 0;

		CONS_Printf("[PS3] Queuing initial Conversion and Flip...\n");
		fflush(stdout);
		ret = cellRescSetConvertAndFlip(gCellGcmCurrentContext, 0);
		CONS_Printf("[PS3] cellRescSetConvertAndFlip returned: %d\n", ret);
		fflush(stdout);

		CONS_Printf("[PS3] Flushing GCM command buffer...\n");
		fflush(stdout);
		cellGcmFlush(gCellGcmCurrentContext);

		CONS_Printf("[PS3] Waiting for initial Flip event...\n");
		fflush(stdout);
		cellRescSetWaitFlip(gCellGcmCurrentContext);

		CONS_Printf("[PS3] Native Libgcm video output and Hardware cellResc scaling initialized successfully.\n");
		fflush(stdout);
	}
	else
	{
		// Fallback to PSGL for hardware renderers
		PSGLinitOptions options;
		PSGLdeviceParameters params;

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
	}

	CV_RegisterVar(&cv_vidwait);

	// Automatically set default resolution mode
	VID_SetMode(0);

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
	if (host_addr)
	{
		cellRescExit();
		free(host_addr);
		host_addr = NULL;
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

			if (display_buffer[0] && display_buffer[1])
			{
				current_buffer_idx = (current_buffer_idx + 1) % 2;

				cellRescSetWaitFlip(gCellGcmCurrentContext);

				uint32_t* dst = (uint32_t*)display_buffer[current_buffer_idx];
				convert_palette_1to1(dst, screens[0], vid.width * vid.height, gl_palette);

				cellRescSetConvertAndFlip(gCellGcmCurrentContext, current_buffer_idx);
				cellGcmFlush(gCellGcmCurrentContext);
			}
			else
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