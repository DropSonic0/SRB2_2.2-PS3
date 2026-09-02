/*
 * SPU Audio Mixer Types and Shared Data Structures
 * PlayStation 3 Cell Broadband Engine
 */

#ifndef __SPU_AUDIO_TYPES_H__
#define __SPU_AUDIO_TYPES_H__

#include <stdint.h>

#define SPU_AUDIO_MAX_CHANNELS 32
#define SPU_AUDIO_BLOCK_SAMPLES 256

/*
 * SPU Audio Channel parameters.
 * Must be aligned to 16 bytes and size must be a multiple of 16 bytes for DMA transfers.
 */
typedef struct __attribute__((aligned(16))) spu_audio_channel
{
	uint64_t ea_data;          /* PPU Effective Address of sample data buffer */
	uint64_t ea_end;           /* PPU Effective Address of end of sample data */

	uint32_t pos_bytes;        /* Current playback byte offset in sample data */
	uint32_t step;             /* Resampling step (fixed point 16.16) */
	uint32_t step_remainder;   /* Fractional accumulator for step */
	int32_t  vol_left;         /* Left volume (0..255) */

	int32_t  vol_right;        /* Right volume (0..255) */
	uint32_t active;           /* 1 if active, 0 if idle */
	uint32_t channel_id;       /* Unique handle ID */
	uint32_t pad0;             /* Padding for 16-byte alignment */
} spu_audio_channel_t;

/*
 * Main SPU Audio Control structure.
 * Passed from PPU to SPU via DMA or Mailbox.
 */
typedef struct __attribute__((aligned(128))) spu_audio_control
{
	uint64_t ea_out_stream;    /* PPU EA of output PCM stream buffer (int16_t stereo) */
	uint64_t ea_music_stream;  /* PPU EA of input music PCM stream buffer (int16_t stereo) */

	uint32_t num_frames;       /* Number of stereo frames to mix (e.g. 256) */
	uint32_t num_channels;     /* Number of active SFX channels */
	int32_t  music_vol_left;   /* Music left volume (0..255) */
	int32_t  music_vol_right;  /* Music right volume (0..255) */

	spu_audio_channel_t channels[SPU_AUDIO_MAX_CHANNELS];
} spu_audio_control_t;

#endif /* __SPU_AUDIO_TYPES_H__ */
