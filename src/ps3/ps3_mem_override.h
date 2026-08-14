// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 2025 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file   ps3_mem_override.h
/// \brief Memory redirection to the custom PS3 pool for specific performance-critical files.

#ifndef _PS3_MEM_OVERRIDE_H_
#define _PS3_MEM_OVERRIDE_H_

#ifdef __CELLOS_LV2__
#include <stddef.h>
#include <stdlib.h> // Carga los prototipos de C nativos ANTES de redefinirlos con macros

#ifdef __cplusplus
extern "C" {
#endif
void *I_PS3_Malloc(size_t size);
void  I_PS3_Free(void *ptr);
void *I_PS3_Realloc(void *ptr, size_t size);
void *I_PS3_Calloc(size_t nmemb, size_t size);
#ifdef __cplusplus
}
#endif

#undef malloc
#undef free
#undef realloc
#undef calloc

#define malloc(x)       I_PS3_Malloc(x)
#define free(x)         I_PS3_Free(x)
#define realloc(x, y)   I_PS3_Realloc(x, y)
#define calloc(x, y)    I_PS3_Calloc(x, y)
#endif

#endif