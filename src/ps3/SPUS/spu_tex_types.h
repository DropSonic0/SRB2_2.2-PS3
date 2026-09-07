/* SCE CONFIDENTIAL
 * PlayStation(R)3 Programmer Tool Runtime Library
 * SPU Texture Conversion Shared Types (libdma getting_argp)
 */

#ifndef SPU_TEX_TYPES_H
#define SPU_TEX_TYPES_H

#include <stdint.h>

#define SPU_TEX_FMT_P_8                1
#define SPU_TEX_FMT_AP_88               2
#define SPU_TEX_FMT_ALPHA_INTENSITY_88 3
#define SPU_TEX_FMT_ALPHA_8            4

/* Exact 1:1 structure layout matching SDK libdma getting_argp/shared_types.h */
struct argument {
	uint64_t	returnAddress;
	uint64_t	frameBuffer0;
	uint64_t	frameBuffer1;
	uint32_t	frameSize;
	uint32_t	nDecoder;
	uint32_t	control;
} __attribute__((aligned(16)));

typedef struct __attribute__((aligned(128))) spu_tex_task
{
	uint64_t ea_src;        /* EA of source texture pixels in PPU main RAM (frameBuffer0) */
	uint64_t ea_dst;        /* EA of destination RGBA pixels in PPU main RAM (frameBuffer1) */
	uint64_t ea_palette;    /* EA of 256-color RGBA palette in PPU main RAM (returnAddress) */
	uint32_t width;         /* Texture width in pixels */
	uint32_t height;        /* Texture height in pixels */
	uint32_t format;        /* SPU_TEX_FMT_... (nDecoder) */
	uint32_t flags;         /* TF_CHROMAKEYED, etc. (control) */
	uint32_t transparent;   /* Output flag: 1 if transparent pixels detected */
	uint8_t  padding[76];   /* Pad to 128 bytes for MFC DMA alignment */
} spu_tex_task_t;

#endif /* SPU_TEX_TYPES_H */
