/*
 * SPU Audio Mixer Implementation
 * PlayStation 3 Cell Broadband Engine SPU Core
 */

#if defined(__SPU__) || defined(__CELLOS_LV2__)

#if defined(__SPU__)
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#else
#include <stdint.h>
#include <string.h>
#endif

#include "spu_audio_types.h"

#define TAG_CTRL 1
#define TAG_MUSIC 2
#define TAG_SFX 3
#define TAG_OUT 4

/* SPU Local Store Buffers (Aligned to 128 bytes for optimal DMA) */
static spu_audio_control_t g_spu_ctrl __attribute__((aligned(128)));
static int16_t g_music_buf[SPU_AUDIO_BLOCK_SAMPLES * 2] __attribute__((aligned(128)));
static int16_t g_out_buf[SPU_AUDIO_BLOCK_SAMPLES * 2] __attribute__((aligned(128)));
static int32_t g_mix_acc[SPU_AUDIO_BLOCK_SAMPLES * 2] __attribute__((aligned(128)));
static uint8_t g_sfx_sample_buf[1024] __attribute__((aligned(128)));

/*
 * Clamp 32-bit integer sample to 16-bit signed PCM range.
 */
static inline int16_t clamp_s16(int32_t sample)
{
	if (sample > 32767) return 32767;
	if (sample < -32768) return -32768;
	return (int16_t)sample;
}

/*
 * SPU Core Audio Mixing Function
 */
void spu_mix_audio_block(uint64_t ctrl_ea)
{
#if defined(__SPU__)
	/* Fetch control structure from PPU RAM via MFC DMA */
	mfc_get(&g_spu_ctrl, ctrl_ea, sizeof(spu_audio_control_t), TAG_CTRL, 0, 0);
	mfc_write_tag_mask(1 << TAG_CTRL);
	mfc_read_tag_status_all();

	uint32_t frames = g_spu_ctrl.num_frames;
	if (frames > SPU_AUDIO_BLOCK_SAMPLES) {
		frames = SPU_AUDIO_BLOCK_SAMPLES;
	}

	/* Clear mixing accumulator */
	for (uint32_t i = 0; i < frames * 2; i++) {
		g_mix_acc[i] = 0;
	}

	/* Fetch music stream if present */
	if (g_spu_ctrl.ea_music_stream != 0) {
		mfc_get(g_music_buf, g_spu_ctrl.ea_music_stream, frames * 4, TAG_MUSIC, 0, 0);
		mfc_write_tag_mask(1 << TAG_MUSIC);
		mfc_read_tag_status_all();

		/* Mix music into accumulator with volume scaling */
		for (uint32_t i = 0; i < frames; i++) {
			int32_t left_sample = (int32_t)g_music_buf[i * 2];
			int32_t right_sample = (int32_t)g_music_buf[i * 2 + 1];

			g_mix_acc[i * 2]     += (left_sample * g_spu_ctrl.music_vol_left) >> 8;
			g_mix_acc[i * 2 + 1] += (right_sample * g_spu_ctrl.music_vol_right) >> 8;
		}
	}

	/* Mix active SFX channels */
	for (uint32_t c = 0; c < SPU_AUDIO_MAX_CHANNELS; c++) {
		spu_audio_channel_t *chan = &g_spu_ctrl.channels[c];
		if (!chan->active || chan->ea_data == 0) {
			continue;
		}

		/* Fetch sample data block for channel */
		uint32_t bytes_to_fetch = sizeof(g_sfx_sample_buf);
		mfc_get(g_sfx_sample_buf, chan->ea_data + chan->pos_bytes, bytes_to_fetch, TAG_SFX, 0, 0);
		mfc_write_tag_mask(1 << TAG_SFX);
		mfc_read_tag_status_all();

		uint32_t pos = 0;
		for (uint32_t i = 0; i < frames; i++) {
			if (chan->pos_bytes + pos >= (uint32_t)(chan->ea_end - chan->ea_data)) {
				chan->active = 0;
				break;
			}

			uint8_t raw_sample = g_sfx_sample_buf[pos];
			int32_t sample = ((int32_t)raw_sample - 128) << 8;

			g_mix_acc[i * 2]     += (sample * chan->vol_left) >> 8;
			g_mix_acc[i * 2 + 1] += (sample * chan->vol_right) >> 8;

			chan->step_remainder += chan->step;
			pos += (chan->step_remainder >> 16);
			chan->step_remainder &= 0xFFFF;
		}

		chan->pos_bytes += pos;
	}

	/* Saturate accumulator into 16-bit PCM output buffer */
	for (uint32_t i = 0; i < frames * 2; i++) {
		g_out_buf[i] = clamp_s16(g_mix_acc[i]);
	}

	/* Output mixed PCM buffer back to PPU RAM via MFC DMA */
	mfc_put(g_out_buf, g_spu_ctrl.ea_out_stream, frames * 4, TAG_OUT, 0, 0);
	mfc_write_tag_mask(1 << TAG_OUT);
	mfc_read_tag_status_all();

	/* Send updated control state back to PPU */
	mfc_put(&g_spu_ctrl, ctrl_ea, sizeof(spu_audio_control_t), TAG_CTRL, 0, 0);
	mfc_write_tag_mask(1 << TAG_CTRL);
	mfc_read_tag_status_all();

#else
	(void)ctrl_ea;
#endif
}

#if defined(__SPU__)
int main(unsigned long long spe_id, unsigned long long argp, unsigned long long envp)
{
	(void)spe_id;
	(void)envp;

	uint32_t ctrl_ea = (uint32_t)argp;

	while (1) {
		/* Read Mailbox signal from PPU to process next audio block */
		uint32_t msg = spu_read_in_mbox();
		if (msg == 0xFFFFFFFF) {
			break; /* Exit command */
		}

		spu_mix_audio_block((uint64_t)ctrl_ea);

		/* Signal PPU completion via Mailbox */
		spu_write_out_mbox(1);
	}

	return 0;
}
#endif

#endif /* __SPU__ || __CELLOS_LV2__ */
