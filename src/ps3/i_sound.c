#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <cell/audio.h>
#include <sys/event.h>
#include <pthread.h>
#include <sys/timer.h>
#include <sys/spu_thread.h>
#include <sys/spu_thread_group.h>
#include <sys/spu_initialize.h>
#include <sys/spu_image.h>
#include <stdlib.h>

#include "SPUS/spu_audio_types.h"

#ifdef true
#undef true
#endif
#ifdef false
#undef false
#endif

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_MAX_CHANNELS 2
#include "stb_vorbis.h"

#ifdef true
#undef true
#endif
#ifdef false
#undef false
#endif

#include "../i_sound.h"
#include "../doomstat.h"
#include "../sounds.h"
#include "../s_sound.h"
#include "../w_wad.h"
#include "../z_zone.h"
#include "../byteptr.h"
#include "../console.h"

#define AUDIO_BLOCKS 16
#define AUDIO_CHANNELS 2
#define CELL_AUDIO_BLOCK_SAMPLES 256

typedef int16_t audio_input_t;

typedef struct fifo_buffer {
	char *buffer;
	size_t size;
	size_t write_offset;
	size_t read_offset;
} fifo_buffer_t;

typedef struct
{
	float tmp_data[CELL_AUDIO_BLOCK_SAMPLES * AUDIO_CHANNELS];
	uint32_t audio_port;
	volatile boolean quit_thread;
	fifo_buffer_t *buffer;
	uint64_t input_rate;

	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t cond;
} ps3_audio_t;

static ps3_audio_t *aud = NULL;

static boolean g_spu_audio_enabled = false;
static sys_spu_thread_t g_spu_thread_id;
static sys_spu_thread_group_t g_spu_group_id;
static spu_audio_control_t g_spu_control_block __attribute__((aligned(128)));
extern char _binary_spu_SPU_Audio_elf_start[];
extern char _binary_spu_SPU_Audio_elf_end[];
static sys_spu_image_t g_spu_image;
static int16_t g_spu_out_buf[CELL_AUDIO_BLOCK_SAMPLES * AUDIO_CHANNELS] __attribute__((aligned(128)));
static int16_t g_spu_music_buf[SPU_MUSIC_SRC_MAX_FRAMES * 2] __attribute__((aligned(128)));
static int16_t g_music_carry_buf[SPU_MUSIC_SRC_MAX_FRAMES * 2];
static uint32_t g_music_carry_count = 0;

/* Prototipos: I_DispatchSPUMixBlock se llama desde event_loop, que en este
 * archivo aparece ANTES de la definición real de las 4 funciones del SPU. */
static boolean I_InitSPUAudioMixer(void);
static void I_FillSPUMusicBuffer(uint32_t frames);
static void I_RetireSPUMusicBuffer(void);
static void I_DispatchSPUMixBlock(int16_t *out, int frames);
static void I_PlaySong_DoOpen(boolean looping);

static inline fifo_buffer_t *fifo_new(size_t size)
{
	fifo_buffer_t *fifo = (fifo_buffer_t*)Z_Calloc(sizeof(*fifo), PU_STATIC, NULL);
	if (!fifo) return NULL;
	fifo->buffer = (char*)Z_Malloc(size, PU_STATIC, NULL);
	if (!fifo->buffer) {
		Z_Free(fifo);
		return NULL;
	}
	memset(fifo->buffer, 0, size);
	fifo->size = size;
	return fifo;
}

static inline void fifo_free(fifo_buffer_t *fifo)
{
	if (fifo) {
		Z_Free(fifo->buffer);
		Z_Free(fifo);
	}
}

static inline size_t fifo_read_avail(const fifo_buffer_t *fifo)
{
	if (fifo->write_offset >= fifo->read_offset)
		return fifo->write_offset - fifo->read_offset;
	return fifo->size - (fifo->read_offset - fifo->write_offset);
}

static inline size_t fifo_write_avail(const fifo_buffer_t *fifo)
{
	size_t avail = fifo->size - fifo_read_avail(fifo);
	return avail > 0 ? avail - 1 : 0;
}

static inline void fifo_read(fifo_buffer_t *fifo, void *data, size_t size)
{
	char *dest = (char*)data;
	size_t first_part = fifo->size - fifo->read_offset;
	if (size <= first_part) {
		memcpy(dest, fifo->buffer + fifo->read_offset, size);
		fifo->read_offset = (fifo->read_offset + size) % fifo->size;
	}
	else {
		memcpy(dest, fifo->buffer + fifo->read_offset, first_part);
		memcpy(dest + first_part, fifo->buffer, size - first_part);
		fifo->read_offset = size - first_part;
	}
}

static inline void fifo_write(fifo_buffer_t *fifo, const void *data, size_t size)
{
	const char *src = (const char*)data;
	size_t first_part = fifo->size - fifo->write_offset;
	if (size <= first_part) {
		memcpy(fifo->buffer + fifo->write_offset, src, size);
		fifo->write_offset = (fifo->write_offset + size) % fifo->size;
	}
	else {
		memcpy(fifo->buffer + fifo->write_offset, src, first_part);
		memcpy(fifo->buffer, src + first_part, size - first_part);
		fifo->write_offset = size - first_part;
	}
}

static inline void array_to_float(float *out, int16_t *in, size_t samples)
{
	size_t i;
	for (i = 0; i < samples; i++) {
		out[i] = (float)in[i] / 32768.0f;
	}
}

/* -------------------------------------------
SOFTWARE MIXER IMPLEMENTATION
------------------------------------------- */

#define NUM_CHANNELS 32

typedef int16_t Sint16;
typedef uint16_t Uint16;
typedef int32_t Sint32;
typedef uint32_t Uint32;
typedef uint8_t Uint8;
typedef int8_t Sint8;

typedef struct chan_struct
{
	Uint8 *data;
	Uint8 *end;

	Uint32 realstep;
	Uint32 step;
	Uint32 stepremainder;
	Uint32 samplerate;

	tic_t starttic;

	INT32 handle;

	void *id;
	sfxenum_t sfxid;
	INT32 vol;
	INT32 sep;
	INT32 leftvol;
	INT32 rightvol;

	Sint16* leftvol_lookup;
	Sint16* rightvol_lookup;
} chan_t;

static chan_t channels[NUM_CHANNELS];

static INT32 steptable[256];
static Sint16 vol_lookup[128 * 256];

static struct {
	int freq;
	int channels;
} audio;

UINT8 sound_started = 0;

typedef struct __attribute__((packed))
{
	Uint16 header;
	Uint16 samplerate;
	Uint32 samples;
	Uint8  data[0];
} dssfx_t;

static inline uint16_t wave_read_u16(const uint8_t *ptr) {
	return ptr[0] | (ptr[1] << 8);
}

static inline uint32_t wave_read_u32(const uint8_t *ptr) {
	return ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
}

static inline int16_t wave_read_s16(const uint8_t *ptr) {
	return (int16_t)(ptr[0] | (ptr[1] << 8));
}

static inline void I_SetChannelParams(chan_t *c, INT32 vol, INT32 sep, INT32 step)
{
	INT32 leftvol;
	INT32 rightvol;
	c->vol = vol;
	c->sep = sep;
	c->step = c->realstep = step;

	if (step != steptable[128])
		c->step += (((c->samplerate << 16) / audio.freq) - 65536);
	else if (c->samplerate != (unsigned)audio.freq)
		c->step = ((c->samplerate << 16) / audio.freq);

	vol = (vol * cv_soundvolume.value) >> 7;

	leftvol = vol - ((vol*sep*sep) >> 16);
	sep = 255 - sep;
	rightvol = vol - ((vol*sep*sep) >> 16);

	if (rightvol < 0)
		rightvol = 0;
	else if (rightvol > 127)
		rightvol = 127;
	if (leftvol < 0)
		leftvol = 0;
	else if (leftvol > 127)
		leftvol = 127;

	c->leftvol = leftvol;
	c->rightvol = rightvol;

	c->leftvol_lookup = &vol_lookup[leftvol * 256];
	c->rightvol_lookup = &vol_lookup[rightvol * 256];
}

static inline void I_SetChannels(void)
{
	INT32 i;
	INT32 j;
	INT32 *steptablemid = steptable + 128;

	for (i = -128; i < 128; i++)
	{
		const double po = pow((double)(2.0l), (double)(i / 64.0l));
		steptablemid[i] = (INT32)(po * 65536.0l);
	}

	for (i = 0; i < 128; i++)
	for (j = 0; j < 256; j++)
	{
		vol_lookup[i * 256 + j] = (Sint16)((i * (j - 128) * 256) / 127);
	}
}

static INT32 FindChannel(INT32 handle)
{
	INT32 i;
	for (i = 0; i < NUM_CHANNELS; i++)
	if (channels[i].handle == handle)
		return i;
	return -1;
}

static INT32 addsfx(sfxenum_t sfxid, INT32 volume, INT32 step, INT32 seperation)
{
	static UINT16 handlenums = 0;
	INT32 i, slot, oldestnum = 0;
	tic_t oldest = gametic;

	for (i = 0; (i < NUM_CHANNELS) && (channels[i].end); i++)
	{
		if (channels[i].starttic < oldest)
		{
			oldestnum = i;
			oldest = channels[i].starttic;
		}
	}

	if (i == NUM_CHANNELS)
		slot = oldestnum;
	else
		slot = i;

	channels[slot].end = NULL;
	channels[slot].data = (Uint8 *)S_sfx[sfxid].data;
	dssfx_t *ds = (dssfx_t *)S_sfx[sfxid].data;
	channels[slot].samplerate = ds->samplerate;
	channels[slot].data += 8;

	while (FindChannel(handlenums) != -1)
	{
		handlenums++;
		if (handlenums == UINT16_MAX)
			handlenums = 0;
	}

	channels[slot].handle = handlenums;
	channels[slot].stepremainder = 0;
	channels[slot].starttic = gametic;

	I_SetChannelParams(&channels[slot], volume, seperation, step);

	channels[slot].id = S_sfx[sfxid].data;
	channels[slot].sfxid = sfxid;
	channels[slot].end = channels[slot].data + S_sfx[sfxid].length;

	return handlenums;
}

static void *getsfx(sfxinfo_t *sfx, size_t *len)
{
	void *lump;
	dssfx_t *paddedsfx = NULL;
	size_t size = *len;

	lump = Z_Malloc(size, PU_STATIC, NULL);
	if (!lump) {
		return NULL;
	}
	W_ReadLump(sfx->lumpnum, lump);

	if (size >= 12 && memcmp(lump, "RIFF", 4) == 0 && memcmp((char*)lump + 8, "WAVE", 4) == 0) {
		size_t data_offset = 12;
		Uint32 raw_data_size = 0;
		size_t raw_data_offset = 0;

		size_t fmt_offset = 12;
		while (fmt_offset + 8 < size) {
			if (memcmp((char*)lump + fmt_offset, "fmt ", 4) == 0) {
				break;
			}
			Uint32 chunk_sz = wave_read_u32((const uint8_t*)lump + fmt_offset + 4);
			fmt_offset += 8 + chunk_sz;
		}

		if (fmt_offset + 24 > size) {
			Z_Free(lump);
			return NULL;
		}

		Uint16 format = wave_read_u16((const uint8_t*)lump + fmt_offset + 8);
		Uint16 channels_num = wave_read_u16((const uint8_t*)lump + fmt_offset + 10);
		Uint32 sample_rate = wave_read_u32((const uint8_t*)lump + fmt_offset + 12);
		Uint16 bits_per_sample = wave_read_u16((const uint8_t*)lump + fmt_offset + 22);

		while (data_offset + 8 < size) {
			if (memcmp((char*)lump + data_offset, "data", 4) == 0) {
				raw_data_size = wave_read_u32((const uint8_t*)lump + data_offset + 4);
				raw_data_offset = data_offset + 8;
				break;
			}
			Uint32 chunk_sz = wave_read_u32((const uint8_t*)lump + data_offset + 4);
			data_offset += 8 + chunk_sz;
		}

		if (raw_data_offset == 0 || raw_data_offset + raw_data_size > size) {
			Z_Free(lump);
			return NULL;
		}

		if (format != 1) {
			Z_Free(lump);
			return NULL;
		}

		if (bits_per_sample == 8) {
			Uint32 num_samples = raw_data_size / channels_num;
			paddedsfx = (dssfx_t *)Z_Malloc(8 + num_samples, PU_SOUND, NULL);
			if (paddedsfx) {
				paddedsfx->header = 3;
				paddedsfx->samplerate = sample_rate;
				paddedsfx->samples = num_samples;

				if (channels_num == 1) {
					memcpy(paddedsfx->data, (char*)lump + raw_data_offset, num_samples);
				}
				else {
					Uint32 i;
					uint8_t *src = (uint8_t*)((char*)lump + raw_data_offset);
					for (i = 0; i < num_samples; i++) {
						paddedsfx->data[i] = src[i * channels_num];
					}
				}
				*len = num_samples;
			}
		}
		else if (bits_per_sample == 16) {
			Uint32 num_samples = raw_data_size / (2 * channels_num);
			paddedsfx = (dssfx_t *)Z_Malloc(8 + num_samples, PU_SOUND, NULL);
			if (paddedsfx) {
				paddedsfx->header = 3;
				paddedsfx->samplerate = sample_rate;
				paddedsfx->samples = num_samples;

				Uint32 i;
				const uint8_t *src_ptr = (const uint8_t*)lump + raw_data_offset;
				for (i = 0; i < num_samples; i++) {
					int16_t s16 = wave_read_s16(src_ptr + i * 2 * channels_num);
					uint8_t u8 = (uint8_t)(((int32_t)s16 + 32768) >> 8);
					paddedsfx->data[i] = u8;
				}
				*len = num_samples;
			}
		}
		else {
			Z_Free(lump);
			return NULL;
		}
	}
	else if (size >= 4 && memcmp(lump, "OggS", 4) == 0) {
		int channels_num = 0;
		int sample_rate = 0;
		short *output = NULL;
		int decoded_samples = stb_vorbis_decode_memory((unsigned char*)lump, size, &channels_num, &sample_rate, &output);

		if (decoded_samples < 0) {
			Z_Free(lump);
			return NULL;
		}

		paddedsfx = (dssfx_t *)Z_Malloc(8 + decoded_samples, PU_SOUND, NULL);
		if (paddedsfx) {
			paddedsfx->header = 3;
			paddedsfx->samplerate = sample_rate;
			paddedsfx->samples = decoded_samples;

			int i;
			for (i = 0; i < decoded_samples; i++) {
				int16_t s16 = output[i * channels_num];
				uint8_t u8 = (uint8_t)(((int32_t)s16 + 32768) >> 8);
				paddedsfx->data[i] = u8;
			}
			*len = decoded_samples;
		}
		free(output);
	}
	else {
		void *stream = lump;
		Uint16 ver = READUINT16(stream);
		Uint16 freq = READUINT16(stream);
		Uint32 samples = READUINT32(stream);

		if (ver != 3) {
			Z_Free(lump);
			return NULL;
		}

		*len = samples;

		paddedsfx = (dssfx_t *)Z_Malloc(size, PU_SOUND, NULL);
		if (paddedsfx) {
			memcpy(paddedsfx, lump, size);
			paddedsfx->header = ver;
			paddedsfx->samplerate = freq;
			paddedsfx->samples = samples;
		}
	}

	Z_Free(lump);
	return paddedsfx;
}

/* -------------------------------------------
MUSIC PLAYER STATE
------------------------------------------- */

static stb_vorbis *music_decoder = NULL;
static char *music_data_buf = NULL;
static size_t music_data_len = 0;
static boolean music_playing = false;
static boolean music_looping = false;
static boolean music_paused = false;
static uint8_t music_volume = 16; // 0 to 31

#ifdef HAVE_XMP
#define LIBXMP_CORE_PLAYER 1
#include <xmp.h>
static xmp_context xmp_ctx = NULL;
static boolean music_playing_xmp = false;
static boolean xmp_module_loaded = false;
static volatile boolean xmp_player_started = false;
#endif

static fifo_buffer_t *music_fifo = NULL;
static double music_accum = 0.0;
static uint32_t music_loop_point_ms = 0;
static uint32_t music_loop_point_samples = 0;

static int music_seek_fail_count = 0;
static volatile boolean music_eof_reached = false;

/* Abrir el decoder de una canción nueva (parsear headers/codebooks de
 * Vorbis) puede tardar unos milisegundos. Si eso se hace en el hilo que
 * llama a I_PlaySong (normalmente el hilo principal del juego), se nota
 * como un microcongelamiento de imagen justo al cambiar de música. Estas
 * dos variables difieren esa apertura al hilo de audio (event_loop), que
 * ya corre aparte y no bloquea el render. */
static volatile boolean pending_song_open = false;
static boolean pending_song_looping = false;
static volatile boolean decoding_in_progress = false;
static volatile float music_fade_factor = 1.0f;

static inline void seek_music_loop(void)
{
	if (!music_decoder) {
		music_playing = false;
		return;
	}

	if (music_loop_point_samples == 0) {
		stb_vorbis_seek_start(music_decoder);
	}
	else {
		if (!stb_vorbis_seek(music_decoder, music_loop_point_samples)) {
			stb_vorbis_seek_start(music_decoder);
		}
	}
	g_music_carry_count = 0;
	g_spu_control_block.music_step_remainder = 0;
}

static void mix_music(Sint16 *stream, int num_frames)
{
#ifdef HAVE_XMP
	if (music_playing_xmp && !music_paused) {
		if (!xmp_player_started) {
			xmp_start_player(xmp_ctx, 48000, 0);
			xmp_player_started = true;
		}
		int16_t xmp_buf[num_frames * 2];
		int play_err = xmp_play_buffer(xmp_ctx, xmp_buf, num_frames * 4, music_looping ? 0 : 1);
		struct xmp_frame_info fi;
		xmp_get_frame_info(xmp_ctx, &fi);
		if (play_err != 0 || (!music_looping && fi.loop_count > 0)) {
			music_playing_xmp = false;
			music_playing = false;
			music_eof_reached = true;
			return;
		}

		int i;
		for (i = 0; i < num_frames; i++) {
			int32_t mixed_r = stream[i * 2];
			int32_t mixed_l = stream[i * 2 + 1];

			if (music_fade_factor < 1.0f) {
				music_fade_factor += 1.0f / 24000.0f;
				if (music_fade_factor > 1.0f) {
					music_fade_factor = 1.0f;
				}
			}

			int32_t vol_r = (int32_t)((((int32_t)xmp_buf[i * 2 + 1] * music_volume / 31) / 2) * music_fade_factor);
			int32_t vol_l = (int32_t)((((int32_t)xmp_buf[i * 2] * music_volume / 31) / 2) * music_fade_factor);

			mixed_l += vol_l;
			mixed_r += vol_r;

			if (mixed_l > 32767) mixed_l = 32767;
			else if (mixed_l < -32768) mixed_l = -32768;

			if (mixed_r > 32767) mixed_r = 32767;
			else if (mixed_r < -32768) mixed_r = -32768;

			stream[i * 2] = (Sint16)mixed_r;
			stream[i * 2 + 1] = (Sint16)mixed_l;
		}
		return;
	}
#endif

	if (!music_playing || music_paused || !music_decoder || !music_fifo)
		return;

	stb_vorbis_info info = stb_vorbis_get_info(music_decoder);
	double step = (double)info.sample_rate / 48000.0;

	int avail_to_read = fifo_read_avail(music_fifo) / 4;
	if (avail_to_read <= 0) {
		if (music_eof_reached) {
			music_playing = false;
		}
		return;
	}

	int i;
	for (i = 0; i < num_frames; i++) {
		double current_idx = i * step + music_accum;
		int idx_i = (int)current_idx;

		if (idx_i >= avail_to_read) {
			break;
		}

		int16_t frame[2];
		size_t target_offset = (music_fifo->read_offset + idx_i * 4) % music_fifo->size;

		if (target_offset + 4 <= music_fifo->size) {
			memcpy(frame, music_fifo->buffer + target_offset, 4);
		}
		else {
			size_t first_part = music_fifo->size - target_offset;
			memcpy(frame, music_fifo->buffer + target_offset, first_part);
			memcpy((char*)frame + first_part, music_fifo->buffer, 4 - first_part);
		}

		int32_t mixed_r = stream[i * 2];
		int32_t mixed_l = stream[i * 2 + 1];

		if (music_fade_factor < 1.0f) {
			music_fade_factor += 1.0f / 24000.0f;
			if (music_fade_factor > 1.0f) {
				music_fade_factor = 1.0f;
			}
		}

		int32_t vol_r = (int32_t)((((int32_t)frame[1] * music_volume / 31) / 2) * music_fade_factor);
		int32_t vol_l = (int32_t)((((int32_t)frame[0] * music_volume / 31) / 2) * music_fade_factor);

		mixed_l += vol_l;
		mixed_r += vol_r;

		if (mixed_l > 32767) mixed_l = 32767;
		else if (mixed_l < -32768) mixed_l = -32768;

		if (mixed_r > 32767) mixed_r = 32767;
		else if (mixed_r < -32768) mixed_r = -32768;

		stream[i * 2] = (Sint16)mixed_r;
		stream[i * 2 + 1] = (Sint16)mixed_l;
	}

	music_accum += i * step;
	int consumed_frames = (int)music_accum;
	if (consumed_frames > avail_to_read) {
		consumed_frames = avail_to_read;
	}

	if (consumed_frames > 0) {
		music_fifo->read_offset = (music_fifo->read_offset + consumed_frames * 4) % music_fifo->size;
		music_accum -= consumed_frames;
	}
}

static inline void I_UpdateStream16S(Uint8 *stream, int len)
{
	register Sint32 dr;
	register Uint8 sample;
	register Sint32 dl;

	Sint16 *rightout = (Sint16 *)(void *)stream;
	Sint16 *leftout = rightout + 1;
	const Uint8 step = 2;

	INT32 chan;

	int frames = len / 4;
	int count = frames;

	while (count--)
	{
		dl = *leftout;
		dr = *rightout;

		for (chan = 0; chan < NUM_CHANNELS; chan++)
		{
			if (channels[chan].end)
			{
				sample = channels[chan].data[0];
				dl += channels[chan].leftvol_lookup[sample];
				dr += channels[chan].rightvol_lookup[sample];

				channels[chan].stepremainder += channels[chan].step;
				if (channels[chan].data + (channels[chan].stepremainder >> 16) >= channels[chan].end)
					channels[chan].end = NULL;
				else
				{
					channels[chan].data += (channels[chan].stepremainder >> 16);
					channels[chan].stepremainder &= 0xffff;
				}
			}
		}

		if (dl > 0x7fff)
			*leftout = 0x7fff;
		else if (dl < -0x8000)
			*leftout = -0x8000;
		else
			*leftout = (Sint16)dl;

		if (dr > 0x7fff)
			*rightout = 0x7fff;
		else if (dr < -0x8000)
			*rightout = -0x8000;
		else
			*rightout = (Sint16)dr;

		leftout += step;
		rightout += step;
	}

	mix_music((Sint16*)stream, frames);
}

static void *event_loop(void *data)
{
	ps3_audio_t *aud_ptr = (ps3_audio_t*)data;
	sys_event_queue_t id;
	sys_ipc_key_t key;
	sys_event_t event;

	cellAudioCreateNotifyEventQueue(&id, &key);
	cellAudioSetNotifyEventQueue(key);

	float out_tmp[CELL_AUDIO_BLOCK_SAMPLES * AUDIO_CHANNELS] __attribute__((aligned(16)));
	int16_t tmp[CELL_AUDIO_BLOCK_SAMPLES * AUDIO_CHANNELS];

	while (!aud_ptr->quit_thread)
	{
		sys_event_queue_receive(id, &event, SYS_NO_TIMEOUT);

		stb_vorbis *dec = NULL;
		boolean loop = false;
		int samples_to_decode = 0;

		pthread_mutex_lock(&aud_ptr->lock);
		if (pending_song_open) {
			pending_song_open = false;
			I_PlaySong_DoOpen(pending_song_looping);
		}
		if (music_playing && !music_paused && !music_eof_reached && music_decoder && music_fifo) {
			int avail_frames = fifo_read_avail(music_fifo) / 4;
			stb_vorbis_info info = stb_vorbis_get_info(music_decoder);
			double step = (double)info.sample_rate / 48000.0;
			int needed = (int)(CELL_AUDIO_BLOCK_SAMPLES * step) + 16;

			if (avail_frames < needed) {
				dec = music_decoder;
				loop = music_looping;
				samples_to_decode = fifo_write_avail(music_fifo) / 4;
				if (samples_to_decode > 2048) {
					samples_to_decode = 2048;
				}
				if (samples_to_decode > 0) {
					decoding_in_progress = true;
				}
			}
		}
		pthread_mutex_unlock(&aud_ptr->lock);

		if (dec && samples_to_decode > 0) {
			static int16_t temp_decode_buf[2048 * 2];
			int decoded = stb_vorbis_get_samples_short_interleaved(dec, 2, temp_decode_buf, samples_to_decode * 2);

			pthread_mutex_lock(&aud_ptr->lock);
			if (music_playing && music_decoder == dec && music_fifo) {
				if (decoded == 0) {
					if (loop) {
						music_seek_fail_count++;
						if (music_seek_fail_count > 3) {
							music_eof_reached = true;
						}
						else {
							seek_music_loop();
						}
					}
					else {
						music_eof_reached = true;
					}
				}
				else {
					music_seek_fail_count = 0;
					fifo_write(music_fifo, temp_decode_buf, decoded * 4);
				}
			}
			decoding_in_progress = false;
			pthread_cond_signal(&aud_ptr->cond);
			pthread_mutex_unlock(&aud_ptr->lock);
		}

		// Mix buffer
		memset(tmp, 0, sizeof(tmp));

		pthread_mutex_lock(&aud_ptr->lock);
		if (sound_started)
		{
			if (g_spu_audio_enabled)
				I_DispatchSPUMixBlock(tmp, CELL_AUDIO_BLOCK_SAMPLES);
			else
				I_UpdateStream16S((Uint8*)tmp, CELL_AUDIO_BLOCK_SAMPLES * 4);
		}
		pthread_mutex_unlock(&aud_ptr->lock);

		array_to_float(out_tmp, tmp, CELL_AUDIO_BLOCK_SAMPLES * AUDIO_CHANNELS);
		cellAudioAddData(aud_ptr->audio_port, out_tmp, CELL_AUDIO_BLOCK_SAMPLES, 1.0);
	}

	cellAudioRemoveNotifyEventQueue(key);
	pthread_exit(NULL);
	return NULL;
}

static boolean cellAudioPortInit(uint64_t samplerate, uint64_t buffersize)
{
	CellAudioPortParam params;

	aud = (ps3_audio_t*)Z_Calloc(sizeof(*aud), PU_STATIC, NULL);
	if (aud == NULL)
		return false;

	cellAudioInit();

	params.nChannel = AUDIO_CHANNELS;
	params.nBlock = AUDIO_BLOCKS;
	params.attr = 0;

	if (cellAudioPortOpen(&params, &aud->audio_port) != CELL_OK)
	{
		cellAudioQuit();
		Z_Free(aud);
		aud = NULL;
		return false;
	}

	aud->buffer = fifo_new(buffersize * sizeof(audio_input_t));
	if (!aud->buffer)
	{
		cellAudioPortClose(aud->audio_port);
		cellAudioQuit();
		Z_Free(aud);
		aud = NULL;
		return false;
	}
	aud->input_rate = samplerate;
	aud->quit_thread = false;

	pthread_mutex_init(&aud->lock, NULL);
	pthread_cond_init(&aud->cond, NULL);

	cellAudioPortStart(aud->audio_port);
	pthread_create(&aud->thread, NULL, event_loop, aud);
	return true;
}

static void cellAudioPortExit(void)
{
	if (!aud) return;

	aud->quit_thread = true;
	pthread_join(aud->thread, NULL);

#ifdef HAVE_XMP
	if (xmp_ctx) {
		xmp_free_context(xmp_ctx);
		xmp_ctx = NULL;
	}
#endif

	cellAudioPortStop(aud->audio_port);
	cellAudioPortClose(aud->audio_port);
	cellAudioQuit();
	fifo_free(aud->buffer);

	pthread_mutex_destroy(&aud->lock);
	pthread_cond_destroy(&aud->cond);
	Z_Free(aud);
	aud = NULL;
}

static void cellAudioPortWrite(const audio_input_t* buf, uint64_t samples)
{
	pthread_mutex_lock(&aud->lock);
	while (fifo_write_avail(aud->buffer) < samples * sizeof(audio_input_t))
	{
		pthread_cond_wait(&aud->cond, &aud->lock);
	}

	fifo_write(aud->buffer, buf, samples * sizeof(audio_input_t));
	pthread_mutex_unlock(&aud->lock);
}

static uint64_t cellAudioPortWriteAvail(void)
{
	uint64_t avail;
	pthread_mutex_lock(&aud->lock);
	avail = fifo_write_avail(aud->buffer) / sizeof(audio_input_t);
	pthread_mutex_unlock(&aud->lock);
	return avail;
}

/* -------------------------------------------
DOOM SYSTEM SOUND DRIVER INTERFACE
------------------------------------------- */

void *I_GetSfx(sfxinfo_t *sfx)
{
	void *result;
	snprintf(z_currently_allocating_asset, sizeof(z_currently_allocating_asset), "SFX: %s", sfx->name);

	if (sfx->lumpnum == LUMPERROR)
		sfx->lumpnum = S_GetSfxLumpNum(sfx);

	if (sfx->data)
	{
		z_currently_allocating_asset[0] = '\0';
		return sfx->data;
	}

	sfx->length = W_LumpLength(sfx->lumpnum);

	result = getsfx(sfx, &sfx->length);
	z_currently_allocating_asset[0] = '\0';
	return result;
}

void I_FreeSfx(sfxinfo_t *sfx)
{
	size_t i;

	pthread_mutex_lock(&aud->lock);

	for (i = 1; i < NUMSFX; i++)
	{
		if (S_sfx[i].data == sfx->data)
		{
			if (S_sfx + i != sfx) S_sfx[i].data = NULL;
			S_sfx[i].lumpnum = LUMPERROR;
			S_sfx[i].length = 0;
		}
	}

	for (i = 0; i < NUM_CHANNELS; i++)
	{
		if (channels[i].end && channels[i].id == sfx->data)
		{
			channels[i].end = NULL;
		}
	}

	Z_Free(sfx->data);
	sfx->data = NULL;
	sfx->lumpnum = LUMPERROR;

	pthread_mutex_unlock(&aud->lock);
}

/*
 * Arranca el thread group del SPU con el mixer embebido.
 * Devuelve false si algo falla (el caller debe caer al mixer de PPU).
 */
static boolean I_InitSPUAudioMixer(void)
{
	int ret;
	sys_spu_thread_group_attribute_t group_attr;
	sys_spu_thread_attribute_t thread_attr;
	sys_spu_thread_argument_t thread_arg;

	ret = sys_spu_initialize(6, 0);
	if (ret != CELL_OK)
	{
		/* 0x8001000a (EBUSY) es el código real que estás viendo en hardware.
		 * sys_spu_initialize está documentado para llamarse UNA vez por
		 * proceso. Si algo más (el loader/CFW, u otra parte del motor) ya
		 * inicializó los SPUs antes de llegar acá, esto no es un fallo real
		 * de recursos -- los SPUs ya están listos para usarse, sólo que no
		 * fuimos nosotros quienes los reclamamos. Seguimos en ese caso
		 * puntual; cualquier otro código de error sí es fatal de verdad. */
		if (ret == 0x8001000a)
		{
			CONS_Printf("I_InitSPUAudioMixer: sys_spu_initialize ya estaba "
			            "inicializado (0x%x), sigo de largo\n", ret);
		}
		else
		{
			CONS_Printf("I_InitSPUAudioMixer: sys_spu_initialize failed (0x%x)\n", ret);
			return false;
		}
	}

	ret = sys_spu_image_import(&g_spu_image, _binary_spu_SPU_Audio_elf_start, SYS_SPU_IMAGE_PROTECT);
	if (ret != CELL_OK)
	{
		CONS_Printf("I_InitSPUAudioMixer: sys_spu_image_import failed (0x%x)\n", ret);
		return false;
	}

	memset(&group_attr, 0, sizeof(group_attr));
	group_attr.name = "srb2_audio_spu_group";
	group_attr.nsize = strlen(group_attr.name) + 1;
	group_attr.type = SYS_SPU_THREAD_GROUP_TYPE_NORMAL;

	ret = sys_spu_thread_group_create(&g_spu_group_id, 1, 100, &group_attr);
	if (ret != CELL_OK)
	{
		CONS_Printf("I_InitSPUAudioMixer: sys_spu_thread_group_create failed (0x%x)\n", ret);
		return false;
	}

	memset(&thread_attr, 0, sizeof(thread_attr));
	thread_attr.name = "srb2_audio_spu_thread";
	thread_attr.nsize = strlen(thread_attr.name) + 1;
	thread_attr.option = SYS_SPU_THREAD_OPTION_NONE;

	memset(&thread_arg, 0, sizeof(thread_arg));
	/* Confirmado por prueba en hardware: arg1->spe_id, arg2->argp, arg3->envp.
	 * Volvemos a la dirección real del control block en arg2. */
	thread_arg.arg2 = (uint64_t)(uintptr_t)&g_spu_control_block;

	ret = sys_spu_thread_initialize(&g_spu_thread_id, g_spu_group_id, 0,
	                                 &g_spu_image, &thread_attr, &thread_arg);
	if (ret != CELL_OK)
	{
		CONS_Printf("I_InitSPUAudioMixer: sys_spu_thread_initialize failed (0x%x)\n", ret);
		sys_spu_thread_group_destroy(g_spu_group_id);
		return false;
	}

	ret = sys_spu_thread_group_start(g_spu_group_id);
	if (ret != CELL_OK)
	{
		CONS_Printf("I_InitSPUAudioMixer: sys_spu_thread_group_start failed (0x%x)\n", ret);
		sys_spu_thread_group_destroy(g_spu_group_id);
		return false;
	}

	return true;
}

/*
 * Arma g_spu_music_buf con hasta SPU_MUSIC_SRC_MAX_FRAMES de PCM nativo
 * (carry del bloque anterior + lo que haga falta sacar del music_fifo),
 * y completa los campos music_* del control block. Llamar con el mutex
 * de aud_ptr->lock ya tomado (misma sección que protege music_fifo).
 */
static void I_FillSPUMusicBuffer(uint32_t frames)
{
	uint32_t needed;
	uint32_t from_fifo;
	uint32_t total;
	uint32_t avail_bytes;

	memset(g_spu_music_buf, 0, sizeof(g_spu_music_buf));
	g_spu_control_block.music_frames_avail = 0;
	g_spu_control_block.music_step = 0;

	if (!(music_playing && !music_paused && !music_eof_reached && music_decoder && music_fifo))
	{
		return;
	}

	{
	stb_vorbis_info info = stb_vorbis_get_info(music_decoder);
	if (info.sample_rate <= 0)
		return;

	g_spu_control_block.music_step =
		(uint32_t)(((double)info.sample_rate / 48000.0) * 65536.0);

	needed = (uint32_t)(((double)frames * info.sample_rate) / 48000.0) + 2;
	if (needed > SPU_MUSIC_SRC_MAX_FRAMES)
		needed = SPU_MUSIC_SRC_MAX_FRAMES;

	total = g_music_carry_count;
	if (total > needed)
		total = needed;
	if (total > 0)
		memcpy(g_spu_music_buf, g_music_carry_buf, (size_t)total * 4);

	from_fifo = needed - total;
	avail_bytes = (uint32_t)fifo_read_avail(music_fifo);
	if (from_fifo > avail_bytes / 4)
		from_fifo = avail_bytes / 4;

	if (from_fifo > 0)
		fifo_read(music_fifo, &g_spu_music_buf[total * 2], (size_t)from_fifo * 4);

	total += from_fifo;
	g_spu_control_block.music_frames_avail = total;
	}
}

/*
 * Llamar DESPUÉS de leer el mailbox de vuelta del SPU (ya con
 * music_frames_consumed actualizado en el control block). Guarda como
 * carry los frames nativos que se mandaron pero no se consumieron.
 */
static void I_RetireSPUMusicBuffer(void)
{
	uint32_t avail = g_spu_control_block.music_frames_avail;
	uint32_t consumed = g_spu_control_block.music_frames_consumed;

	if (consumed > avail)
		consumed = avail;

	g_music_carry_count = avail - consumed;
	if (g_music_carry_count > SPU_MUSIC_SRC_MAX_FRAMES)
		g_music_carry_count = SPU_MUSIC_SRC_MAX_FRAMES;

	if (g_music_carry_count > 0)
		memmove(g_music_carry_buf, &g_spu_music_buf[consumed * 2], (size_t)g_music_carry_count * 4);
}

/*
 * Llena el control block con el estado actual de canales SFX + música,
 * despacha un bloque de mezcla al SPU vía mailbox y espera el resultado.
 */
static void I_DispatchSPUMixBlock(int16_t *out, int frames)
{
	INT32 chan;
	INT32 n = 0;

	I_FillSPUMusicBuffer((uint32_t)frames);

	g_spu_control_block.num_frames = (uint32_t)frames;
	g_spu_control_block.ea_out_stream = (uint64_t)(uintptr_t)g_spu_out_buf;
	g_spu_control_block.ea_music_stream = (uint64_t)(uintptr_t)g_spu_music_buf;
	/* Antes esto estaba hardcodeado a 255 (volumen máximo fijo), por eso
	 * la música sonaba siempre a full mientras los SFX sí respetaban el
	 * volumen real configurado -- desbalance que se sentía como "los SFX
	 * suenan bajo". music_volume ya existe (0..31, la usa el mixer viejo
	 * de música) y nunca se conectó acá. */
	g_spu_control_block.music_vol_left = (music_volume * 255) / 31;
	g_spu_control_block.music_vol_right = (music_volume * 255) / 31;

	for (chan = 0; chan < NUM_CHANNELS; chan++)
	{
		if (!channels[chan].end)
			continue;

		g_spu_control_block.channels[n].ea_data = (uint64_t)(uintptr_t)channels[chan].id;
		g_spu_control_block.channels[n].ea_end = (uint64_t)(uintptr_t)channels[chan].end;
		g_spu_control_block.channels[n].pos_bytes =
			(uint32_t)(channels[chan].data - (Uint8 *)channels[chan].id);
		g_spu_control_block.channels[n].step = channels[chan].step;
		g_spu_control_block.channels[n].step_remainder = channels[chan].stepremainder;

		/* Redondeo en vez de truncar (+63 antes de dividir por 127) para
		 * no perder volumen sistemáticamente en cada multiplicación. */
		g_spu_control_block.channels[n].vol_left = (channels[chan].leftvol * 256 + 63) / 127;
		g_spu_control_block.channels[n].vol_right = (channels[chan].rightvol * 256 + 63) / 127;

		g_spu_control_block.channels[n].active = 1;
		g_spu_control_block.channels[n].channel_id = (uint32_t)channels[chan].handle;
		n++;
	}
	g_spu_control_block.num_channels = (uint32_t)n;

	{
		/* DIAGNÓSTICO: probar si esta implementación de mailbox "dedupea"
		 * o ignora escrituras repetidas con el mismo valor -- hasta ahora
		 * siempre mandábamos el mismo "1". Mandamos un valor que cambia
		 * en cada bloque para descartar esto. También revisamos el
		 * código de retorno, que nunca habíamos chequeado. */
		static uint32_t mbox_send_counter = 1;
		int mbox_ret = sys_spu_thread_write_spu_mb(g_spu_thread_id, mbox_send_counter);
		if (mbox_ret != CELL_OK)
		{
			CONS_Printf("I_DispatchSPUMixBlock: sys_spu_thread_write_spu_mb "
			            "devolvió error 0x%x (valor=%u)\n", mbox_ret, mbox_send_counter);
		}
		mbox_send_counter++;
		if (mbox_send_counter == 0xFFFFFFFF) {
			mbox_send_counter = 1; /* nunca mandar el valor reservado para salir */
		}
	}
	{
		/* La PPU nunca veía block_done actualizado por DMA (probablemente
		 * por caché -- confirmado con diagnóstico que el SPU sí terminaba
		 * el bloque entero, la PPU simplemente no se enteraba). En vez de
		 * confiar en memoria compartida por DMA para la señal de "listo",
		 * usamos sys_spu_thread_read_ls (que en TODAS las pruebas reflejó
		 * siempre el valor real) para leer un contador que el SPU
		 * incrementa en su propio Local Store al terminar cada bloque. */
		static uint32_t last_seen_counter = 0;
		uint32_t spins = 0;
		const uint32_t SPU_WAIT_MAX_SPINS = 5000000; /* DIAGNÓSTICO: mucho más alto que antes, para descartar que simplemente haga falta más tiempo/reintentos */
		uint64_t counter_val = 0;

		for (;;)
		{
			sys_spu_thread_read_ls(g_spu_thread_id, 0xd20, &counter_val, 4);
			if ((uint32_t)counter_val != last_seen_counter)
			{
				last_seen_counter = (uint32_t)counter_val;
				break;
			}

			if (++spins > SPU_WAIT_MAX_SPINS)
			{
				uint64_t started_val = 0, loop_val = 0;
				int exit_status = 0;
				int exit_ret;

				sys_spu_thread_read_ls(g_spu_thread_id, 0xd30, &started_val, 4);
				sys_spu_thread_read_ls(g_spu_thread_id, 0xd40, &loop_val, 4);
				exit_ret = sys_spu_thread_get_exit_status(g_spu_thread_id, &exit_status);

				CONS_Printf("I_DispatchSPUMixBlock: timeout esperando al SPU "
				            "(terminados=%u, iniciados=%u, vueltas_loop=%u, "
				            "exit_ret=0x%x, exit_status=%d)\n",
				            (uint32_t)counter_val, (uint32_t)started_val, (uint32_t)loop_val,
				            exit_ret, exit_status);
				break;
			}
		}
	}

	memcpy(out, g_spu_out_buf, (size_t)frames * 4);

	I_RetireSPUMusicBuffer();

	n = 0;
	for (chan = 0; chan < NUM_CHANNELS; chan++)
	{
		if (!channels[chan].end)
			continue;
		if (!g_spu_control_block.channels[n].active)
			channels[chan].end = NULL;
		else
			channels[chan].data = (Uint8 *)channels[chan].id + g_spu_control_block.channels[n].pos_bytes;
		n++;
	}
}

void I_StartupSound(void)
{
	if (sound_started)
		return;

	audio.freq = 48000;
	audio.channels = 2;

	memset(channels, 0, sizeof(channels));
	I_SetChannels();

#ifdef HAVE_XMP
	if (!xmp_ctx) {
		xmp_ctx = xmp_create_context();
	}
#endif

	if (!cellAudioPortInit(48000, 16384))
	{
		return;
	}

	memset(&g_spu_control_block, 0, sizeof(g_spu_control_block));
	g_spu_audio_enabled = I_InitSPUAudioMixer();

	sound_started = 1;
}

void I_ShutdownSound(void)
{
	if (!sound_started)
		return;

	if (g_spu_audio_enabled) {
		sys_spu_thread_write_spu_mb(g_spu_thread_id, 0xFFFFFFFF);
		sys_spu_thread_group_join(g_spu_group_id, NULL, NULL);
		sys_spu_thread_group_destroy(g_spu_group_id);
		g_spu_audio_enabled = false;
	}

	cellAudioPortExit();
	sound_started = 0;
}

void I_UpdateSound(void)
{
	// Mixed completely on the background thread on-demand!
}

INT32 I_StartSound(sfxenum_t id, UINT8 vol, UINT8 sep, UINT8 pitch, UINT8 priority, INT32 channel)
{
	(void)priority;
	(void)pitch;
	(void)channel;

	if (!sound_started)
		return 0;

	if (S_sfx[id].data == NULL) {
		return -1;
	}

	int handle;
	pthread_mutex_lock(&aud->lock);
	handle = addsfx(id, vol, steptable[pitch], sep);
	pthread_mutex_unlock(&aud->lock);
	return handle;
}

void I_StopSound(INT32 handle)
{
	pthread_mutex_lock(&aud->lock);
	INT32 i = FindChannel(handle);

	if (i != -1)
	{
		channels[i].end = NULL;
		channels[i].handle = -1;
		channels[i].starttic = 0;
	}
	pthread_mutex_unlock(&aud->lock);
}

boolean I_SoundIsPlaying(INT32 handle)
{
	boolean isplaying = false;
	pthread_mutex_lock(&aud->lock);
	int chan = FindChannel(handle);
	if (chan != -1)
		isplaying = (channels[chan].end != NULL);
	pthread_mutex_unlock(&aud->lock);
	return isplaying;
}

void I_UpdateSoundParams(INT32 handle, UINT8 vol, UINT8 sep, UINT8 pitch)
{
	pthread_mutex_lock(&aud->lock);
	INT32 i = FindChannel(handle);

	if (i != -1 && channels[i].end)
	{
		I_SetChannelParams(&channels[i], vol, sep, steptable[pitch]);
	}
	pthread_mutex_unlock(&aud->lock);
}

void I_SetSfxVolume(UINT8 volume)
{
	pthread_mutex_lock(&aud->lock);
	INT32 i;
	(void)volume;

	for (i = 0; i < NUM_CHANNELS; i++)
	if (channels[i].end)
		I_SetChannelParams(&channels[i], channels[i].vol, channels[i].sep, channels[i].realstep);
	pthread_mutex_unlock(&aud->lock);
}

/// ------------------------
//  MUSIC SYSTEM
/// ------------------------

void I_InitMusic(void){}

void I_ShutdownMusic(void){}

/// ------------------------
//  MUSIC PROPERTIES
/// ------------------------

musictype_t I_SongType(void)
{
	return MU_OGG;
}

boolean I_SongPlaying(void)
{
	return music_playing;
}

boolean I_SongPaused(void)
{
	return music_paused;
}

/// ------------------------
//  MUSIC EFFECTS
/// ------------------------

boolean I_SetSongSpeed(float speed)
{
	(void)speed;
	return false;
}

/// ------------------------
//  MUSIC SEEKING
/// ------------------------

UINT32 I_GetSongLength(void)
{
	pthread_mutex_lock(&aud->lock);
	UINT32 len = 0;
#ifdef HAVE_XMP
	if (music_playing_xmp) {
		struct xmp_frame_info fi;
		xmp_get_frame_info(xmp_ctx, &fi);
		len = fi.total_time;
	}
	else
#endif
	if (music_decoder) {
		len = stb_vorbis_stream_length_in_samples(music_decoder) * 1000 / stb_vorbis_get_info(music_decoder).sample_rate;
	}
	pthread_mutex_unlock(&aud->lock);
	return len;
}

boolean I_SetSongLoopPoint(UINT32 looppoint)
{
	pthread_mutex_lock(&aud->lock);
	music_loop_point_ms = looppoint;
	if (music_decoder) {
		stb_vorbis_info info = stb_vorbis_get_info(music_decoder);
		music_loop_point_samples = looppoint * info.sample_rate / 1000;
	}
	pthread_mutex_unlock(&aud->lock);
	return true;
}

UINT32 I_GetSongLoopPoint(void)
{
	return music_loop_point_ms;
}

boolean I_SetSongPosition(UINT32 position)
{
	if (!aud) return false;

	pthread_mutex_lock(&aud->lock);
	while (decoding_in_progress) {
		pthread_cond_wait(&aud->cond, &aud->lock);
	}

	boolean success = false;
#ifdef HAVE_XMP
	if (music_playing_xmp) {
		xmp_seek_time(xmp_ctx, position);
		success = true;
	}
	else
#endif
	if (music_decoder) {
		stb_vorbis_info info = stb_vorbis_get_info(music_decoder);
		unsigned int sample_offset = position * info.sample_rate / 1000;
		if (stb_vorbis_seek(music_decoder, sample_offset)) {
			success = true;
			if (music_fifo) {
				music_fifo->read_offset = 0;
				music_fifo->write_offset = 0;
			}
			music_accum = 0.0;
			music_eof_reached = false;
			music_fade_factor = 0.0f;
			g_music_carry_count = 0;
			g_spu_control_block.music_step_remainder = 0;
		}
	}
	pthread_mutex_unlock(&aud->lock);
	return success;
}

UINT32 I_GetSongPosition(void)
{
	if (!aud) return 0;

	pthread_mutex_lock(&aud->lock);
	while (decoding_in_progress) {
		pthread_cond_wait(&aud->cond, &aud->lock);
	}

	UINT32 pos = 0;
#ifdef HAVE_XMP
	if (music_playing_xmp) {
		struct xmp_frame_info fi;
		xmp_get_frame_info(xmp_ctx, &fi);
		pos = fi.time;
	}
	else
#endif
	if (music_decoder) {
		stb_vorbis_info info = stb_vorbis_get_info(music_decoder);
		if (info.sample_rate > 0) {
			pos = stb_vorbis_get_sample_offset(music_decoder) * 1000 / info.sample_rate;
		}
	}
	pthread_mutex_unlock(&aud->lock);
	return pos;
}

/// ------------------------
//  MUSIC PLAYBACK
/// ------------------------

static void I_StopSong_Internal(void)
{
	music_playing = false;
	music_paused = false;
	music_eof_reached = true;

	if (aud) {
		while (decoding_in_progress) {
			pthread_cond_wait(&aud->cond, &aud->lock);
		}
	}

#ifdef HAVE_XMP
	if (xmp_ctx) {
		xmp_end_player(xmp_ctx);
		xmp_release_module(xmp_ctx);
	}
	music_playing_xmp = false;
	xmp_module_loaded = false;
	xmp_player_started = false;
#endif

	if (music_decoder) {
		stb_vorbis_close(music_decoder);
		music_decoder = NULL;
	}

	if (music_fifo) {
		fifo_free(music_fifo);
		music_fifo = NULL;
	}
	g_music_carry_count = 0;
	g_spu_control_block.music_step_remainder = 0;
}

static void I_UnloadSong_Internal(void)
{
	I_StopSong_Internal();

	if (music_data_buf) {
		Z_Free(music_data_buf);
		music_data_buf = NULL;
	}
	music_data_len = 0;
}

boolean I_LoadSong(char *data, size_t len)
{
	pthread_mutex_lock(&aud->lock);
	I_UnloadSong_Internal();

	if (!data || len == 0) {
		pthread_mutex_unlock(&aud->lock);
		return false;
	}

	music_data_buf = Z_Malloc(len, PU_STATIC, NULL);
	if (!music_data_buf) {
		pthread_mutex_unlock(&aud->lock);
		return false;
	}
	memcpy(music_data_buf, data, len);
	music_data_len = len;

	pthread_mutex_unlock(&aud->lock);
	return true;
}

void I_UnloadSong(void)
{
	pthread_mutex_lock(&aud->lock);
	I_UnloadSong_Internal();
	pthread_mutex_unlock(&aud->lock);
}

/*
 * Hace el trabajo pesado real de arrancar una canción (abrir el decoder,
 * parsear comentarios/loop points, armar el fifo). Se llama con
 * aud->lock ya tomado -- desde event_loop (hilo de audio), de forma
 * diferida, para no bloquear al hilo que llamó a I_PlaySong.
 */
static void I_PlaySong_DoOpen(boolean looping)
{
	int error_code = 0;
	music_decoder = stb_vorbis_open_memory((unsigned char*)music_data_buf, music_data_len, &error_code, NULL);
	if (!music_decoder) {
#ifdef HAVE_XMP
		if (!xmp_ctx) {
			xmp_ctx = xmp_create_context();
		}
		if (xmp_ctx && xmp_load_module_from_memory(xmp_ctx, music_data_buf, music_data_len) == 0) {
			struct xmp_module_info mi;
			xmp_get_module_info(xmp_ctx, &mi);
			CONS_Printf("I_PlaySong: Playing %s format (Title: %s)\n", mi.mod ? mi.mod->type : "Tracker", mi.mod ? mi.mod->name : "Unknown");

			music_looping = looping;
			music_playing_xmp = true;
			xmp_module_loaded = true;
			xmp_player_started = false;
			music_playing = true;
			music_paused = false;
			music_seek_fail_count = 0;
			music_eof_reached = false;
			music_fade_factor = 0.0f;
			return;
		}
#endif
		CONS_Printf("I_PlaySong: Failed to decode music format (Unsupported or corrupt file)\n");
		music_playing = false;
		return;
	}

	stb_vorbis_info info = stb_vorbis_get_info(music_decoder);
	CONS_Printf("I_PlaySong: Playing OGG/Vorbis format (%d Hz, %d channels)\n", info.sample_rate, info.channels);

	// Parse loop tags from OGG comments for precise loop points
	music_loop_point_ms = 0;
	music_loop_point_samples = 0;
	stb_vorbis_comment comment = stb_vorbis_get_comment(music_decoder);
	int i;
	for (i = 0; i < comment.comment_list_length; i++) {
		char *c = comment.comment_list[i];
		char *eq = strchr(c, '=');
		if (eq) {
			size_t tag_len = eq - c;
			char tag_name[32];
			if (tag_len < sizeof(tag_name)) {
				memcpy(tag_name, c, tag_len);
				tag_name[tag_len] = '\0';

				// Strip trailing spaces from tag name
				if (tag_len > 0) {
					char *t_end = tag_name + tag_len - 1;
					while (t_end > tag_name && *t_end == ' ') {
						*t_end = '\0';
						t_end--;
					}
				}

				// Skip leading spaces of the value
				char *val_str = eq + 1;
				while (*val_str == ' ') {
					val_str++;
				}

				if (strcasecmp(tag_name, "LOOPPOINT") == 0 || strcasecmp(tag_name, "LOOP_POINT") == 0 ||
					strcasecmp(tag_name, "LOOPSTART") == 0 || strcasecmp(tag_name, "LOOP_START") == 0) {
					music_loop_point_samples = (uint32_t)atof(val_str);
					music_loop_point_ms = music_loop_point_samples * 1000 / info.sample_rate;
				}
				else if (strcasecmp(tag_name, "LOOPMS") == 0 || strcasecmp(tag_name, "LOOP_MS") == 0) {
					music_loop_point_ms = (uint32_t)atof(val_str);
					music_loop_point_samples = music_loop_point_ms * info.sample_rate / 1000;
				}
			}
		}
	}

	// Fallback raw memory scan (identical to PC/SDL version for 100% compatibility if tags are hidden or malformed)
	if (music_loop_point_samples == 0 && music_data_buf && music_data_len > 12) {
		char *p_scan = music_data_buf;
		size_t len_scan = music_data_len;
		while ((size_t)(p_scan - music_data_buf) < len_scan - 12) {
			if (strncasecmp(p_scan, "LOOPPOINT=", 10) == 0) {
				music_loop_point_samples = (uint32_t)atof(p_scan + 10);
				music_loop_point_ms = music_loop_point_samples * 1000 / info.sample_rate;
				break;
			}
			else if (strncasecmp(p_scan, "LOOP_POINT=", 11) == 0) {
				music_loop_point_samples = (uint32_t)atof(p_scan + 11);
				music_loop_point_ms = music_loop_point_samples * 1000 / info.sample_rate;
				break;
			}
			else if (strncasecmp(p_scan, "LOOPSTART=", 10) == 0) {
				music_loop_point_samples = (uint32_t)atof(p_scan + 10);
				music_loop_point_ms = music_loop_point_samples * 1000 / info.sample_rate;
				break;
			}
			else if (strncasecmp(p_scan, "LOOP_START=", 11) == 0) {
				music_loop_point_samples = (uint32_t)atof(p_scan + 11);
				music_loop_point_ms = music_loop_point_samples * 1000 / info.sample_rate;
				break;
			}
			else if (strncasecmp(p_scan, "LOOPMS=", 7) == 0) {
				music_loop_point_ms = (uint32_t)atof(p_scan + 7);
				music_loop_point_samples = music_loop_point_ms * info.sample_rate / 1000;
				break;
			}
			else if (strncasecmp(p_scan, "LOOP_MS=", 8) == 0) {
				music_loop_point_ms = (uint32_t)atof(p_scan + 8);
				music_loop_point_samples = music_loop_point_ms * info.sample_rate / 1000;
				break;
			}
			p_scan++;
		}
	}

	if (!music_fifo) {
		music_fifo = fifo_new(65536);
	}
	else {
		music_fifo->read_offset = 0;
		music_fifo->write_offset = 0;
	}
	music_accum = 0.0;
	g_music_carry_count = 0;
	g_spu_control_block.music_step_remainder = 0;

	music_looping = looping;
	music_playing = true;
	music_paused = false;
	music_seek_fail_count = 0;
	music_eof_reached = false;
	music_fade_factor = 0.0f;
}

boolean I_PlaySong(boolean looping)
{
	pthread_mutex_lock(&aud->lock);
	I_StopSong_Internal();

	if (!music_data_buf || music_data_len == 0) {
		pthread_mutex_unlock(&aud->lock);
		return false;
	}

	/* No abrimos el decoder acá -- se lo pasamos al hilo de audio para
	 * que lo haga en segundo plano (ver I_PlaySong_DoOpen), y así no
	 * bloqueamos al hilo que llamó a I_PlaySong (frena la imagen si es
	 * el hilo principal del juego). */
	pending_song_looping = looping;
	pending_song_open = true;
	pthread_cond_signal(&aud->cond);

	pthread_mutex_unlock(&aud->lock);
	return true;
}

void I_StopSong(void)
{
	pthread_mutex_lock(&aud->lock);
	I_StopSong_Internal();
	pthread_mutex_unlock(&aud->lock);
}

void I_PauseSong(void)
{
	pthread_mutex_lock(&aud->lock);
	music_paused = true;
	pthread_mutex_unlock(&aud->lock);
}

void I_ResumeSong(void)
{
	pthread_mutex_lock(&aud->lock);
	music_paused = false;
	pthread_mutex_unlock(&aud->lock);
}

void I_SetMusicVolume(UINT8 volume)
{
	pthread_mutex_lock(&aud->lock);
	music_volume = volume;
	pthread_mutex_unlock(&aud->lock);
}

boolean I_SetSongTrack(INT32 track)
{
	(void)track;
	return false;
}

/// ------------------------
//  MUSIC FADING
/// ------------------------

void I_SetInternalMusicVolume(UINT8 volume)
{
	(void)volume;
}

void I_StopFadingSong(void)
{
}

boolean I_FadeSongFromVolume(UINT8 target_volume, UINT8 source_volume, UINT32 ms, void(*callback)(void))
{
	(void)target_volume;
	(void)source_volume;
	(void)ms;
	if (callback) {
		callback();
	}
	return true;
}

boolean I_FadeSong(UINT8 target_volume, UINT32 ms, void(*callback)(void))
{
	(void)target_volume;
	(void)ms;
	if (callback) {
		callback();
	}
	return true;
}

boolean I_FadeOutStopSong(UINT32 ms)
{
	(void)ms;
	I_StopSong();
	return true;
}

boolean I_FadeInPlaySong(UINT32 ms, boolean looping)
{
	(void)ms;
	return I_PlaySong(looping);
}
