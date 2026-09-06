/*
 * SPU Audio Mixer Implementation
 * PlayStation 3 Cell Broadband Engine SPU Core
 */

#if defined(__SPU__) || defined(__CELLOS_LV2__)

#if defined(__SPU__)
#include <spu_intrinsics.h>
#include <cell/dma.h>
#include <stddef.h>
#else
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#endif

#include "spu_audio_types.h"

#define TAG_CTRL 1
#define TAG_MUSIC 2
#define TAG_SFX 3
#define TAG_OUT 4

/* SPU Local Store Buffers (Aligned to 128 bytes for optimal DMA) */
static spu_audio_control_t g_spu_ctrl __attribute__((aligned(128)));
static int16_t g_music_src_buf[SPU_MUSIC_SRC_MAX_FRAMES * 2] __attribute__((aligned(128)));
static int16_t g_out_buf[SPU_AUDIO_BLOCK_SAMPLES * 2] __attribute__((aligned(128)));
static int32_t g_mix_acc[SPU_AUDIO_BLOCK_SAMPLES * 2] __attribute__((aligned(128)));
static uint8_t g_sfx_sample_buf[1024] __attribute__((aligned(128)));

volatile uint32_t g_ppu_probe_alive = 0;
volatile uint32_t g_ppu_probe_spe_id = 0;
volatile uint32_t g_ppu_probe_argp = 0;
volatile uint32_t g_ppu_probe_envp = 0;
volatile uint32_t g_ppu_probe_mbox_seen = 0;
volatile uint32_t g_ppu_probe_mbox_value = 0;
volatile uint32_t g_ppu_probe_get_done = 0;
volatile uint32_t g_ppu_probe_big_get_done = 0;
volatile uint32_t g_ppu_probe_out_put_done = 0;
volatile uint32_t g_ppu_probe_final_put_done = 0;
volatile uint32_t g_spu_block_counter = 0;
volatile uint32_t g_spu_block_started_counter = 0;
volatile uint32_t g_spu_loop_iterations = 0;

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
	g_spu_block_started_counter++;

	{
		uint32_t test_buf[4] __attribute__((aligned(16)));
		cellDmaGet(test_buf, ctrl_ea, 16, TAG_CTRL, 0, 0);
		cellDmaWaitTagStatusAll(1 << TAG_CTRL);
		g_ppu_probe_get_done = 0xDEADBEEFU;
	}

	/* Fetch control structure from PPU RAM via MFC DMA */
	cellDmaGet(&g_spu_ctrl, ctrl_ea, sizeof(spu_audio_control_t), TAG_CTRL, 0, 0);
	cellDmaWaitTagStatusAll(1 << TAG_CTRL);
	g_ppu_probe_big_get_done = 0xB16DEAD1U;

	uint32_t frames = g_spu_ctrl.num_frames;
	if (frames > SPU_AUDIO_BLOCK_SAMPLES) {
		frames = SPU_AUDIO_BLOCK_SAMPLES;
	}

	/* Clear mixing accumulator */
	for (uint32_t i = 0; i < frames * 2; i++) {
		g_mix_acc[i] = 0;
	}

	g_spu_ctrl.music_frames_consumed = 0;

	/* Fetch + resample music stream (native rate -> 48kHz) if present */
	if (g_spu_ctrl.ea_music_stream != 0 && g_spu_ctrl.music_frames_avail > 0) {
		uint32_t fetch_frames = g_spu_ctrl.music_frames_avail;
		if (fetch_frames > SPU_MUSIC_SRC_MAX_FRAMES) {
			fetch_frames = SPU_MUSIC_SRC_MAX_FRAMES;
		}

		uint32_t fetch_bytes = fetch_frames * 4;

		fetch_bytes = (fetch_bytes + 15u) & ~15u;
		if (fetch_bytes < 16) {
			fetch_bytes = 16;
		}

		cellDmaGet(g_music_src_buf, g_spu_ctrl.ea_music_stream, fetch_bytes, TAG_MUSIC, 0, 0);
		cellDmaWaitTagStatusAll(1 << TAG_MUSIC);

		uint32_t src_pos = 0;
		uint32_t step_rem = g_spu_ctrl.music_step_remainder;

		for (uint32_t i = 0; i < frames; i++) {
			if (src_pos >= fetch_frames) {
				break;
			}

			int32_t left_sample = (int32_t)g_music_src_buf[src_pos * 2];
			int32_t right_sample = (int32_t)g_music_src_buf[src_pos * 2 + 1];

			g_mix_acc[i * 2]     += (left_sample * g_spu_ctrl.music_vol_left) >> 8;
			g_mix_acc[i * 2 + 1] += (right_sample * g_spu_ctrl.music_vol_right) >> 8;

			step_rem += g_spu_ctrl.music_step;
			src_pos += (step_rem >> 16);
			step_rem &= 0xFFFF;
		}

		g_spu_ctrl.music_step_remainder = step_rem;
		g_spu_ctrl.music_frames_consumed = (src_pos > fetch_frames) ? fetch_frames : src_pos;
	}

	/* Mix active SFX channels */
	for (uint32_t c = 0; c < SPU_AUDIO_MAX_CHANNELS; c++) {
		spu_audio_channel_t *chan = &g_spu_ctrl.channels[c];
		if (!chan->active || chan->ea_data == 0) {
			continue;
		}

		uint32_t src_ea = chan->ea_data + chan->pos_bytes;
		uint32_t align_offset = src_ea & 0xF;
		uint32_t aligned_ea = src_ea - align_offset;
		uint32_t bytes_to_fetch = sizeof(g_sfx_sample_buf);

		cellDmaGet(g_sfx_sample_buf, aligned_ea, bytes_to_fetch, TAG_SFX, 0, 0);
		cellDmaWaitTagStatusAll(1 << TAG_SFX);

		uint32_t chan_len = (uint32_t)(chan->ea_end - chan->ea_data);
		uint32_t pos = 0;
		for (uint32_t i = 0; i < frames; i++) {
			if (chan->pos_bytes + pos >= chan_len) {
				chan->active = 0;
				break;
			}

			if (align_offset + pos >= sizeof(g_sfx_sample_buf)) {
				break;
			}

			uint8_t raw_sample = g_sfx_sample_buf[align_offset + pos];
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
	cellDmaPut(g_out_buf, g_spu_ctrl.ea_out_stream, frames * 4, TAG_OUT, 0, 0);
	cellDmaWaitTagStatusAll(1 << TAG_OUT);
	g_ppu_probe_out_put_done = 0xABCD1234U;

	g_spu_ctrl.block_done = 1;
	cellDmaPut(&g_spu_ctrl, ctrl_ea, sizeof(spu_audio_control_t), TAG_CTRL, 0, 0);
	cellDmaWaitTagStatusAll(1 << TAG_CTRL);
	g_ppu_probe_final_put_done = 0xF14A1CADU;

	g_spu_block_counter++;

#else
	(void)ctrl_ea;
#endif
}

#if defined(__SPU__)
int main(unsigned long long spe_id, unsigned long long argp, unsigned long long envp)
{
	(void)spe_id;
	(void)envp;

	g_ppu_probe_alive = 0xDEADC0DEU;

	g_ppu_probe_spe_id = (uint32_t)spe_id;
	g_ppu_probe_argp = (uint32_t)argp;
	g_ppu_probe_envp = (uint32_t)envp;

	uint32_t ctrl_ea = (uint32_t)argp;

	while (1) {
		g_spu_loop_iterations++;

		uint32_t msg = spu_read_in_mbox();

		g_ppu_probe_mbox_seen = 0xB0B0CAFEU;
		g_ppu_probe_mbox_value = msg;

		if (msg == 0xFFFFFFFF) {
			break; /* Exit command */
		}

		spu_mix_audio_block((uint64_t)ctrl_ea);

	}

	return 0;
}
#endif

#endif /* __SPU__ || __CELLOS_LV2__ */
