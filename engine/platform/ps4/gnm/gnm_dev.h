/*
gnm_dev.h - GNM device for PS4 OpenGL backend: memory, display, command submission
Copyright (C) 2026 sergiopoverony

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef GNM_DEV_H
#define GNM_DEV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <gnm_commandbuffer.h>
#include <gnm_depthrendertarget.h>
#include <gnm_rendertarget.h>
#include <gnm_shaderbinary.h>

// Command submission is synchronous: every submit waits for the GPU, so
// everything allocated for the frame can be reused right after a submit.
// Big frames are split into several submits (gnm_dev_reserve).

typedef struct gnm_vs_s
{
	GnmVsShader *shader; // CPU copy of the header, registers point to GPU code
	int table_reg;       // user SGPR with descriptor table pointer, -1 if none
} gnm_vs_t;

typedef struct gnm_ps_s
{
	GnmPsShader *shader;
	int table_reg;
} gnm_ps_t;

typedef struct gnm_dev_s
{
	int width, height;
	GnmRenderTarget color[2];
	GnmDepthRenderTarget depth;
	int back; // color target being rendered

	GnmCommandBuffer cmd;
	uint32_t submit_serial; // increments with every submit, see gnm_dev_flush
	uint32_t frame;

	// diagnostics
	int debug;              // -gnmdebug level, see PS4_GL_Init
	bool stats;             // -gnmstats: frame timing in the log every 300 frames
	bool hung;              // GPU didn't finish a submit, nothing is submitted anymore
	uint32_t submit_draws;  // draws recorded into the current command buffer
	char last_draw[192];    // description of the last draw, filled with debug enabled
} gnm_dev_t;

extern gnm_dev_t gnm_dev;

bool gnm_dev_init( void );
void gnm_dev_shutdown( void );

// called after the command buffer was (re)started: everything has to be emitted again
typedef void (*gnm_dev_restart_cb)( void );
void gnm_dev_set_restart_callback( gnm_dev_restart_cb cb );

// makes room for the next draw: command buffer space, descriptor tables
// and transient memory; submits what's recorded if there's not enough
GnmCommandBuffer *gnm_dev_reserve( uint32_t dwords, uint32_t tables, uint32_t transient );

// submits recorded commands and waits for them, frees per-submit memory
void gnm_dev_flush( const char *reason );

// submits the frame, shows it and starts the next one
void gnm_dev_present( void );

// per-submit memory, valid until the next flush, returns NULL if not reserved
// tables: descriptor tables, below 4 GB (shaders get 32-bit pointers to them)
void *gnm_dev_alloc_table( uint32_t size );
// transient: vertex, index and uniform data
void *gnm_dev_alloc_transient( uint32_t size, uint32_t align );

// GPU memory for textures and buffers
void *gnm_dev_heap_alloc( size_t size, size_t align );
// freed memory is reused after the GPU is done with the current submit
void gnm_dev_heap_free( void *ptr );

// shader binary from opengnm-psbc
bool gnm_dev_load_vs( gnm_vs_t *out, const void *data, size_t size );
bool gnm_dev_load_ps( gnm_ps_t *out, const void *data, size_t size );

// writes one user SGPR (descriptor table pointer, psbc passes it as 32 bits)
void gnm_dev_set_user_sgpr( GnmCommandBuffer *cmd, bool pixel, int reg, uint32_t value );
// writes context registers directly (no opengnm helper for them)
void gnm_dev_set_context_reg( GnmCommandBuffer *cmd, uint32_t reg, uint32_t value );

#endif // GNM_DEV_H
