/*
 * SPU Texture Format Converter Implementation
 * PlayStation 3 Cell Broadband Engine SPU Core
 */

#if defined(__SPU__) || defined(__CELLOS_LV2__)

#if defined(__SPU__)
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <spu_printf.h>
#else
#include <stdint.h>
#include <string.h>
#endif

#include "spu_tex_types.h"

#define TAG_TASK 1
#define TAG_SRC  2
#define TAG_PAL  3
#define TAG_DST  4

#define SPU_TEX_CHUNK_PIXELS 1024

/* SPU Local Store Buffers aligned to 128 bytes for DMA */
static spu_tex_task_t g_spu_task __attribute__((aligned(128)));
static uint32_t g_spu_palette[256] __attribute__((aligned(128)));
static uint8_t g_spu_src_buf[SPU_TEX_CHUNK_PIXELS * 2] __attribute__((aligned(128)));
static uint32_t g_spu_dst_buf[SPU_TEX_CHUNK_PIXELS] __attribute__((aligned(128)));

void spu_convert_texture(uint64_t task_ea)
{
#if defined(__SPU__)
	/* Native SPU MFC DMA transfer without requiring external libdma.a */
	mfc_get(&g_spu_task, task_ea, sizeof(spu_tex_task_t), TAG_TASK, 0, 0);
	mfc_write_tag_mask(1 << TAG_TASK);
	mfc_read_tag_status_all();

	spu_printf("[SPU-Tex] Processing texture %ux%u (format=%u, EA_src=0x%llx, EA_dst=0x%llx)\n",
		g_spu_task.width, g_spu_task.height, g_spu_task.format,
		(unsigned long long)g_spu_task.ea_src, (unsigned long long)g_spu_task.ea_dst);

	uint32_t total_pixels = g_spu_task.width * g_spu_task.height;
	uint32_t transparent = 0;

	/* Fetch palette if needed */
	if (g_spu_task.format == SPU_TEX_FMT_P_8 || g_spu_task.format == SPU_TEX_FMT_AP_88) {
		if (g_spu_task.ea_palette != 0) {
			mfc_get(g_spu_palette, g_spu_task.ea_palette, 256 * sizeof(uint32_t), TAG_PAL, 0, 0);
			mfc_write_tag_mask(1 << TAG_PAL);
			mfc_read_tag_status_all();
		}
	}

	uint32_t processed = 0;
	while (processed < total_pixels) {
		uint32_t chunk = total_pixels - processed;
		if (chunk > SPU_TEX_CHUNK_PIXELS) chunk = SPU_TEX_CHUNK_PIXELS;

		uint32_t src_bytes_per_pixel = (g_spu_task.format == SPU_TEX_FMT_AP_88 || g_spu_task.format == SPU_TEX_FMT_ALPHA_INTENSITY_88) ? 2 : 1;
		uint32_t fetch_bytes = (chunk * src_bytes_per_pixel + 15) & ~15;

		mfc_get(g_spu_src_buf, g_spu_task.ea_src + (processed * src_bytes_per_pixel), fetch_bytes, TAG_SRC, 0, 0);
		mfc_write_tag_mask(1 << TAG_SRC);
		mfc_read_tag_status_all();

		if (g_spu_task.format == SPU_TEX_FMT_P_8) {
			for (uint32_t i = 0; i < chunk; i++) {
				uint8_t idx = g_spu_src_buf[i];
				if (idx == 255 && (g_spu_task.flags & 1)) {
					g_spu_dst_buf[i] = 0;
					transparent = 1;
				} else {
					g_spu_dst_buf[i] = g_spu_palette[idx];
				}
			}
		} else if (g_spu_task.format == SPU_TEX_FMT_AP_88) {
			for (uint32_t i = 0; i < chunk; i++) {
				uint8_t idx = g_spu_src_buf[i * 2];
				uint8_t alpha = g_spu_src_buf[i * 2 + 1];
				uint32_t color = g_spu_palette[idx];
				if (!(g_spu_task.flags & 1)) {
					color = (color & 0x00FFFFFF) | ((uint32_t)alpha << 24);
				}
				if (alpha < 255) transparent = 1;
				g_spu_dst_buf[i] = color;
			}
		} else if (g_spu_task.format == SPU_TEX_FMT_ALPHA_INTENSITY_88) {
			for (uint32_t i = 0; i < chunk; i++) {
				uint8_t intensity = g_spu_src_buf[i * 2];
				uint8_t alpha = g_spu_src_buf[i * 2 + 1];
				g_spu_dst_buf[i] = ((uint32_t)alpha << 24) | ((uint32_t)intensity << 16) | ((uint32_t)intensity << 8) | (uint32_t)intensity;
			}
		} else if (g_spu_task.format == SPU_TEX_FMT_ALPHA_8) {
			for (uint32_t i = 0; i < chunk; i++) {
				uint8_t alpha = g_spu_src_buf[i];
				g_spu_dst_buf[i] = ((uint32_t)alpha << 24) | 0x00FFFFFF;
			}
		}

		uint32_t put_bytes = (chunk * sizeof(uint32_t) + 15) & ~15;
		mfc_put(g_spu_dst_buf, g_spu_task.ea_dst + (processed * sizeof(uint32_t)), put_bytes, TAG_DST, 0, 0);
		mfc_write_tag_mask(1 << TAG_DST);
		mfc_read_tag_status_all();

		processed += chunk;
	}

	g_spu_task.transparent = transparent;
	mfc_put(&g_spu_task, task_ea, sizeof(spu_tex_task_t), TAG_TASK, 0, 0);
	mfc_write_tag_mask(1 << TAG_TASK);
	mfc_read_tag_status_all();

	spu_printf("[SPU-Tex] Completed texture %ux%u (transparent=%u)\n",
		g_spu_task.width, g_spu_task.height, g_spu_task.transparent);
#else
	(void)task_ea;
#endif
}

#if defined(__SPU__)
int main(unsigned long long spe_id, unsigned long long argp, unsigned long long envp)
{
	(void)spe_id;
	(void)envp;

	spu_printf("[SPU-Tex] SPU Texture Converter thread initialized (SPE ID=0x%llx)\n", spe_id);

	uint32_t task_ea = (uint32_t)argp;

	while (1) {
		uint32_t msg = spu_read_in_mbox();
		if (msg == 0xFFFFFFFF) break;

		spu_convert_texture((uint64_t)task_ea);
		spu_write_out_mbox(1);
	}

	spu_printf("[SPU-Tex] SPU Texture Converter thread exiting\n");
	return 0;
}
#endif

#endif /* __SPU__ || __CELLOS_LV2__ */
