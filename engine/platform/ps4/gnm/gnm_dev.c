/*
gnm_dev.c - GNM device for PS4 OpenGL backend: memory, display, command submission
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>

#include <gnm.h>
#include <gnm_dataformat.h>
#include <gnm_drawcommandbuffer.h>
#include <gnmdriver.h>
#include <gpuaddr.h>
#include <compat/pssl.h>
#include <pm4/sid.h>

#include "gnm_dev.h"
#include "gnm_engine.h"

#define GNM_LOG( ... ) printf( "GNM: " __VA_ARGS__ )

#define CMD_SIZE        ( GNM_INDIRECT_BUFFER_MAX_BYTESIZE & ~0xff ) // per submit
#define TABLE_POOL_SIZE ( 4 * 1024 * 1024 )
#define TRANSIENT_SIZE  ( 64 * 1024 * 1024 )
#define HEAP_BLOCK_SIZE ( 128 * 1024 * 1024 )
#define HEAP_MAX_BLOCKS 16
#define WAIT_TIMEOUT_US 3000000

#define GARLIC_PROT ( ORBIS_KERNEL_PROT_CPU_READ | ORBIS_KERNEL_PROT_CPU_RW | ORBIS_KERNEL_PROT_GPU_READ | ORBIS_KERNEL_PROT_GPU_WRITE )

gnm_dev_t gnm_dev;

/*
==============================================================================

Direct memory

==============================================================================
*/
static void *map_garlic( size_t size, size_t align, uint64_t hint, bool below_4gb )
{
	static const int flags[] = { 0, 0x10 | 0x80 }; // none, then SCE_KERNEL_MAP_FIXED | SCE_KERNEL_MAP_NO_OVERWRITE
	off_t off = 0;
	size_t f;
	int res;

	res = sceKernelAllocateDirectMemory( 0, sceKernelGetDirectMemorySize(), size, align, ORBIS_KERNEL_WC_GARLIC, &off );
	if( res < 0 )
	{
		GNM_LOG( "AllocateDirectMemory 0x%zx failed: 0x%x\n", size, res );
		return NULL;
	}

	for( f = 0; f < ( below_4gb ? 2 : 1 ); f++ )
	{
		void *addr = (void *)(uintptr_t)hint;

		res = sceKernelMapDirectMemory( &addr, size, GARLIC_PROT, flags[f], off, align );
		if( res < 0 )
			continue;

		if( !below_4gb || (uintptr_t)addr + size <= 0x100000000ULL )
			return addr;

		sceKernelMunmap( addr, size );
	}

	GNM_LOG( "MapDirectMemory 0x%zx failed: 0x%x\n", size, res );
	sceKernelReleaseDirectMemory( off, size );
	return NULL;
}

/*
==============================================================================

GPU heap: first fit allocator over big garlic blocks

==============================================================================
*/
typedef struct heap_chunk_s
{
	uintptr_t addr;
	size_t size;
	bool used;
	struct heap_chunk_s *next; // sorted by address within a block
} heap_chunk_t;

typedef struct heap_free_s
{
	void *ptr;
	struct heap_free_s *next;
} heap_free_t;

static struct
{
	heap_chunk_t *blocks[HEAP_MAX_BLOCKS];
	int numblocks;
	heap_free_t *pending; // freed during the current submit
} heap;

static bool heap_add_block( size_t minsize )
{
	size_t size = HEAP_BLOCK_SIZE;
	heap_chunk_t *c;
	void *mem;

	if( heap.numblocks >= HEAP_MAX_BLOCKS )
		return false;

	while( size < minsize )
		size *= 2;

	mem = map_garlic( size, 64 * 1024, 0, false );
	if( !mem )
		return false;

	c = calloc( 1, sizeof( *c ));
	c->addr = (uintptr_t)mem;
	c->size = size;
	heap.blocks[heap.numblocks++] = c;
	GNM_LOG( "heap block %d: %zu MB at %p\n", heap.numblocks, size >> 20, mem );
	return true;
}

static void *heap_try_alloc( size_t size, size_t align )
{
	int b;

	for( b = 0; b < heap.numblocks; b++ )
	{
		heap_chunk_t *c;

		for( c = heap.blocks[b]; c; c = c->next )
		{
			uintptr_t start, end;

			if( c->used )
				continue;

			start = ( c->addr + align - 1 ) & ~( (uintptr_t)align - 1 );
			end = start + size;

			if( end > c->addr + c->size )
				continue;

			// split off the tail
			if( end < c->addr + c->size )
			{
				heap_chunk_t *tail = calloc( 1, sizeof( *tail ));
				tail->addr = end;
				tail->size = c->addr + c->size - end;
				tail->next = c->next;
				c->next = tail;
				c->size = end - c->addr;
			}

			// alignment gap stays inside the chunk, it's freed together with it
			c->used = true;
			return (void *)start;
		}
	}

	return NULL;
}

void *gnm_dev_heap_alloc( size_t size, size_t align )
{
	void *ptr;

	if( align < 256 )
		align = 256;
	size = ( size + 255 ) & ~(size_t)255;

	ptr = heap_try_alloc( size, align );
	if( !ptr && heap_add_block( size + align ))
		ptr = heap_try_alloc( size, align );

	if( !ptr )
		GNM_LOG( "out of GPU memory, %zu bytes requested\n", size );

	return ptr;
}

static void heap_release( void *ptr )
{
	int b;

	for( b = 0; b < heap.numblocks; b++ )
	{
		heap_chunk_t *c, *prev = NULL;

		for( c = heap.blocks[b]; c; prev = c, c = c->next )
		{
			if( !c->used || (uintptr_t)ptr < c->addr || (uintptr_t)ptr >= c->addr + c->size )
				continue;

			c->used = false;

			// merge with free neighbours
			if( c->next && !c->next->used )
			{
				heap_chunk_t *n = c->next;
				c->size += n->size;
				c->next = n->next;
				free( n );
			}

			if( prev && !prev->used )
			{
				prev->size += c->size;
				prev->next = c->next;
				free( c );
			}
			return;
		}
	}

	GNM_LOG( "heap: freeing unknown pointer %p\n", ptr );
}

void gnm_dev_heap_free( void *ptr )
{
	heap_free_t *f;

	if( !ptr )
		return;

	f = malloc( sizeof( *f ));
	f->ptr = ptr;
	f->next = heap.pending;
	heap.pending = f;
}

static void heap_release_pending( void )
{
	while( heap.pending )
	{
		heap_free_t *f = heap.pending;
		heap.pending = f->next;
		heap_release( f->ptr );
		free( f );
	}
}

/*
==============================================================================

Per-submit memory

==============================================================================
*/
static struct
{
	uint8_t *tables;
	uint32_t tables_used;
	uint8_t *transient;
	uint32_t transient_used;
	uint32_t *cmdmem;
	volatile uint64_t *label;
	gnm_dev_restart_cb restart;

	int videohandle;
	OrbisKernelEqueue flipqueue;
	bool initialized;
} dev;

/*
==============================================================================

Frame timing (-gnmstats)

A frame is the time between two presents. GPU is the time spent waiting for
submits to finish, flip is the wait for vsync, CPU is everything else: the
game and recording of commands.

==============================================================================
*/
#define STATS_FRAMES 300

static struct
{
	uint64_t frame_start;
	uint64_t gpu, flip;         // this frame
	uint32_t submits, draws;    // this frame

	uint32_t frames;            // in the period
	uint64_t sum_frame, sum_cpu, sum_gpu, sum_flip;
	uint64_t max_frame, max_cpu, max_gpu;
	uint64_t sum_submits, sum_draws;
	uint64_t period_start;
} stats;

static void stats_end_frame( void )
{
	const uint64_t now = sceKernelGetProcessTime();
	uint64_t frame, cpu;

	if( !stats.frame_start )
	{
		stats.frame_start = stats.period_start = now;
		stats.gpu = stats.flip = 0;
		stats.submits = stats.draws = 0;
		return;
	}

	frame = now - stats.frame_start;
	cpu = frame > stats.gpu + stats.flip ? frame - stats.gpu - stats.flip : 0;

	stats.frames++;
	stats.sum_frame += frame;
	stats.sum_cpu += cpu;
	stats.sum_gpu += stats.gpu;
	stats.sum_flip += stats.flip;
	stats.sum_submits += stats.submits;
	stats.sum_draws += stats.draws;
	if( frame > stats.max_frame ) stats.max_frame = frame;
	if( cpu > stats.max_cpu ) stats.max_cpu = cpu;
	if( stats.gpu > stats.max_gpu ) stats.max_gpu = stats.gpu;

	if( stats.frames >= STATS_FRAMES )
	{
		const double n = stats.frames;

		if( gnm_dev.stats )
		{
			GNM_LOG( "stats: %.1f fps; frame %.2f ms (max %.2f); cpu %.2f ms (max %.2f); gpu %.2f ms (max %.2f); vsync wait %.2f ms; %.1f submits, %.0f draws per frame\n",
				n * 1000000.0 / (double)( now - stats.period_start ),
				stats.sum_frame / n / 1000.0, stats.max_frame / 1000.0,
				stats.sum_cpu / n / 1000.0, stats.max_cpu / 1000.0,
				stats.sum_gpu / n / 1000.0, stats.max_gpu / 1000.0,
				stats.sum_flip / n / 1000.0,
				stats.sum_submits / n, stats.sum_draws / n );
		}

		memset( &stats, 0, sizeof( stats ));
		stats.period_start = now;
	}

	stats.frame_start = now;
	stats.gpu = stats.flip = 0;
	stats.submits = stats.draws = 0;
}

// allocations never submit by themselves, gnm_dev_reserve makes room for them
// before anything of a draw is recorded, so nothing gets lost in the middle
void *gnm_dev_alloc_table( uint32_t size )
{
	uint32_t start = ( dev.tables_used + 15 ) & ~15u;

	if( start + size > TABLE_POOL_SIZE )
	{
		GNM_LOG( "descriptor table pool overflow, reserve more\n" );
		return NULL;
	}

	dev.tables_used = start + size;
	return dev.tables + start;
}

void *gnm_dev_alloc_transient( uint32_t size, uint32_t align )
{
	uint32_t start;

	if( align < 16 )
		align = 16;

	start = ( dev.transient_used + align - 1 ) & ~( align - 1 );

	if( size > TRANSIENT_SIZE )
	{
		GNM_LOG( "transient allocation of %u bytes is too big\n", size );
		return NULL;
	}

	if( start + size > TRANSIENT_SIZE )
	{
		GNM_LOG( "transient memory overflow, reserve more\n" );
		return NULL;
	}

	dev.transient_used = start + size;
	return dev.transient + start;
}

/*
==============================================================================

Command buffer

==============================================================================
*/
void gnm_dev_set_user_sgpr( GnmCommandBuffer *cmd, bool pixel, int reg, uint32_t value )
{
	// SPI_SHADER_USER_DATA_PS_0 / SPI_SHADER_USER_DATA_VS_0
	const uint32_t base = pixel ? R_00B030_SPI_SHADER_USER_DATA_PS_0 : R_00B130_SPI_SHADER_USER_DATA_VS_0;

	if( reg < 0 || cmd->cmdptr + 3 > cmd->endptr )
		return;

	cmd->cmdptr[0] = PKT3( PKT3_SET_SH_REG, 1, 0 );
	cmd->cmdptr[1] = ( base - SI_SH_REG_OFFSET ) / 4 + reg;
	cmd->cmdptr[2] = value;
	cmd->cmdptr += 3;
}

void gnm_dev_set_context_reg( GnmCommandBuffer *cmd, uint32_t reg, uint32_t value )
{
	if( cmd->cmdptr + 3 > cmd->endptr )
		return;

	cmd->cmdptr[0] = PKT3( PKT3_SET_CONTEXT_REG, 1, 0 );
	cmd->cmdptr[1] = ( reg - SI_CONTEXT_REG_OFFSET ) / 4;
	cmd->cmdptr[2] = value;
	cmd->cmdptr += 3;
}

// CPU wrote textures, buffers and shaders to memory GPU caches could have seen
// before with different contents, so invalidate everything at the start of a submit
static void emit_cache_invalidate( GnmCommandBuffer *cmd )
{
	cmd->cmdptr[0] = PKT3( PKT3_ACQUIRE_MEM, 5, 0 );
	cmd->cmdptr[1] = S_0301F0_SH_ICACHE_ACTION_ENA( 1 ) | S_0301F0_SH_KCACHE_ACTION_ENA( 1 ) |
		S_0301F0_TCL1_ACTION_ENA( 1 ) | S_0301F0_TC_ACTION_ENA( 1 );
	cmd->cmdptr[2] = 0xffffffff; // CP_COHER_SIZE
	cmd->cmdptr[3] = 0x000000ff; // CP_COHER_SIZE_HI
	cmd->cmdptr[4] = 0;          // CP_COHER_BASE
	cmd->cmdptr[5] = 0;          // CP_COHER_BASE_HI
	cmd->cmdptr[6] = 0xa;        // poll interval
	cmd->cmdptr += 7;
}

static void begin_cmd( void )
{
	GnmCommandBuffer *cmd = &gnm_dev.cmd;

	*cmd = sceGnmCmdInit( dev.cmdmem, CMD_SIZE, NULL, NULL );
	sceGnmDrawCmdInitDefaultHardwareState( cmd );
	emit_cache_invalidate( cmd );

	sceGnmDrawCmdSetRenderTarget( cmd, 0, &gnm_dev.color[gnm_dev.back] );
	sceGnmDrawCmdSetDepthRenderTarget( cmd, &gnm_dev.depth );

	if( dev.restart )
		dev.restart();
}

static bool wait_label( uint64_t value )
{
	uint64_t start = sceKernelGetProcessTime();

	while( *dev.label != value )
	{
		if( sceKernelGetProcessTime() - start > WAIT_TIMEOUT_US )
			return false;
		sceKernelUsleep( 30 );
	}

	return true;
}

static void submit_and_wait( const char *reason )
{
	GnmCommandBuffer *cmd = &gnm_dev.cmd;
	const uint64_t value = 0x474e4d0000000000ULL | ++gnm_dev.submit_serial;
	void *dcb[1];
	uint32_t dcbsize[1];
	uint64_t start;
	int res;

	// after a GPU hang more work could take the whole console down, the game keeps
	// running without picture so the log can be taken and the game can be closed
	if( gnm_dev.hung )
	{
		dev.tables_used = 0;
		dev.transient_used = 0;
		heap_release_pending();
		gnm_dev.submit_draws = 0;
		return;
	}

	// make sure color and depth writes land in memory before the label
	sceGnmDrawCmdWaitGraphicsWrite( cmd, GNM_ACQUIRE_TARGET_CB0 | GNM_ACQUIRE_TARGET_DB );
	sceGnmDrawCmdEventWriteEop( cmd, GNM_CACHE_FLUSH_AND_INV_TS_EVENT, (uint64_t)(uintptr_t)dev.label, GNM_DATA_SEL_SEND_DATA64, value );

	dcb[0] = cmd->beginptr;
	dcbsize[0] = (uint32_t)((uintptr_t)cmd->cmdptr - (uintptr_t)cmd->beginptr );

	if( gnm_dev.debug == 2 || ( gnm_dev.debug == 1 && gnm_dev.frame < 3 ))
		GNM_LOG( "submit %u (%s): frame %u, %u draws, %u bytes, tables %u, transient %u\n", gnm_dev.submit_serial, reason,
			gnm_dev.frame, gnm_dev.submit_draws, dcbsize[0], dev.tables_used, dev.transient_used );

	stats.submits++;
	stats.draws += gnm_dev.submit_draws;

	start = sceKernelGetProcessTime();
	res = sceGnmSubmitCommandBuffers( 1, dcb, dcbsize, NULL, NULL );
	if( res < 0 )
		GNM_LOG( "submit (%s, %u bytes) failed: 0x%x\n", reason, dcbsize[0], res );
	else if( !wait_label( value ))
	{
		gnm_dev.hung = true;
		GNM_LOG( "GPU HANG: submit %u (%s) not finished in %d ms, frame %u, %u draws, %u bytes, last label 0x%llx\n",
			gnm_dev.submit_serial, reason, WAIT_TIMEOUT_US / 1000, gnm_dev.frame, gnm_dev.submit_draws, dcbsize[0],
			(unsigned long long)*dev.label );
		GNM_LOG( "GPU HANG: last draw: %s\n", gnm_dev.last_draw[0] ? gnm_dev.last_draw : "unknown, run with -gnmdebug" );
		GNM_LOG( "GPU HANG: rendering stopped, close the game\n" );
	}
	else if( gnm_dev.debug == 2 )
		GNM_LOG( "submit %u done in %llu us\n", gnm_dev.submit_serial, (unsigned long long)( sceKernelGetProcessTime() - start ));

	stats.gpu += sceKernelGetProcessTime() - start;

	// everything used by this submit is free now
	dev.tables_used = 0;
	dev.transient_used = 0;
	heap_release_pending();
	gnm_dev.submit_draws = 0;
}

GnmCommandBuffer *gnm_dev_reserve( uint32_t dwords, uint32_t tables, uint32_t transient )
{
	GnmCommandBuffer *cmd = &gnm_dev.cmd;

	// alignment of every allocation is included generously
	tables += 256;
	transient += 4096;

	if( transient > TRANSIENT_SIZE )
		transient = TRANSIENT_SIZE;

	// room for the reservation and for the end of submit
	if( cmd->cmdptr + dwords + 64 > cmd->endptr )
		gnm_dev_flush( "command buffer" );
	else if( dev.tables_used + tables > TABLE_POOL_SIZE )
		gnm_dev_flush( "descriptor tables" );
	else if( dev.transient_used + transient > TRANSIENT_SIZE )
		gnm_dev_flush( "transient memory" );

	return cmd;
}

void gnm_dev_flush( const char *reason )
{
	static uint32_t last_frame_logged = ~0u;

	if( !dev.initialized )
		return;

	if( last_frame_logged != gnm_dev.frame && gnm_dev.frame < 1000 && gnm_dev.debug < 3 )
	{
		GNM_LOG( "frame %u split, out of %s\n", gnm_dev.frame, reason );
		last_frame_logged = gnm_dev.frame;
	}

	submit_and_wait( reason );
	begin_cmd();
}

void gnm_dev_present( void )
{
	OrbisKernelEvent ev;
	int out = 0, res;
	uint64_t flip_start;

	if( !dev.initialized )
		return;

	submit_and_wait( "frame" );
	if( gnm_dev.hung )
	{
		gnm_dev.frame++;
		begin_cmd();
		sceKernelUsleep( 16000 ); // keep the game loop at about the same speed
		return;
	}
	sceGnmSubmitDone();

	flip_start = sceKernelGetProcessTime();
	res = sceVideoOutSubmitFlip( dev.videohandle, gnm_dev.back, ORBIS_VIDEO_OUT_FLIP_VSYNC, 0 );
	if( res < 0 )
		GNM_LOG( "SubmitFlip failed: 0x%x\n", res );
	else
		sceKernelWaitEqueue( dev.flipqueue, &ev, 1, &out, NULL );
	stats.flip += sceKernelGetProcessTime() - flip_start;
	stats_end_frame();

	gnm_dev.back ^= 1;
	gnm_dev.frame++;
	begin_cmd();
}

void gnm_dev_set_restart_callback( gnm_dev_restart_cb cb )
{
	dev.restart = cb;
}

/*
==============================================================================

Shaders

==============================================================================
*/
static int find_table_register( const GnmInputUsageSlot *slots, unsigned count )
{
	unsigned i;

	for( i = 0; i < count; i++ )
	{
		if( slots[i].usagetype == GNM_SHINPUTUSAGE_PTR_INDIRECTRESOURCETABLE )
			return slots[i].startregister;
	}

	return -1;
}

static const GnmShaderCommonData *parse_shader( const void *data, size_t size, const uint32_t **code )
{
	const GnmShaderFileHeader *header = (const GnmShaderFileHeader *)((const uint8_t *)data + sizeof( PsslBinaryHeader ));
	const GnmShaderCommonData *common = (const GnmShaderCommonData *)((const uint8_t *)header + sizeof( *header ));
	const uint32_t *sbaddr = (const uint32_t *)((const uint8_t *)common + sizeof( *common ));

	if( size < sizeof( PsslBinaryHeader ) + sizeof( *header ) + sizeof( *common ) + 8 || header->magic != GNM_SHADER_FILE_HEADER_ID )
	{
		GNM_LOG( "bad shader binary\n" );
		return NULL;
	}

	*code = (const uint32_t *)((const uint8_t *)common + sbaddr[0] );
	return common;
}

static void *upload_code( const GnmShaderCommonData *common, const uint32_t *code )
{
	uint32_t size = sceGnmShaderCommonCodeSize( common );
	void *gpu = gnm_dev_heap_alloc( size, GNM_ALIGNMENT_SHADER_BYTES );

	if( gpu )
		memcpy( gpu, code, size );

	return gpu;
}

bool gnm_dev_load_vs( gnm_vs_t *out, const void *data, size_t size )
{
	const uint32_t *code;
	const GnmShaderCommonData *common = parse_shader( data, size, &code );
	const GnmVsShader *vs = (const GnmVsShader *)common;
	uint32_t hdrsize;
	void *gpu;

	if( !common || !( gpu = upload_code( common, code )))
		return false;

	hdrsize = sceGnmVsShaderCalcSize( vs );
	out->shader = malloc( hdrsize );
	memcpy( out->shader, vs, hdrsize );
	sceGnmVsRegsSetAddress( &out->shader->registers, gpu );
	out->table_reg = find_table_register( sceGnmVsShaderInputUsageSlotTable( out->shader ), out->shader->common.numinputusageslots );
	return true;
}

bool gnm_dev_load_ps( gnm_ps_t *out, const void *data, size_t size )
{
	const uint32_t *code;
	const GnmShaderCommonData *common = parse_shader( data, size, &code );
	const GnmPsShader *ps = (const GnmPsShader *)common;
	uint32_t hdrsize;
	void *gpu;

	if( !common || !( gpu = upload_code( common, code )))
		return false;

	hdrsize = sceGnmPsShaderCalcSize( ps );
	out->shader = malloc( hdrsize );
	memcpy( out->shader, ps, hdrsize );
	sceGnmPsRegsSetAddress( &out->shader->registers, gpu );
	out->table_reg = find_table_register( sceGnmPsShaderInputUsageSlotTable( out->shader ), out->shader->common.numinputusageslots );
	return true;
}

/*
==============================================================================

Display

==============================================================================
*/
static bool init_color_target( GnmRenderTarget *rt, int width, int height )
{
	GpaSurfaceProperties props;
	GnmRenderTargetCreateInfo ci;
	uint64_t size = 0;
	uint32_t align = 0;
	GpaError gerr;
	GnmError err;
	void *mem;

	memset( &props, 0, sizeof( props ));
	gerr = sceGpaFindOptimalSurface( &props, GPA_SURFACE_COLORDISPLAY, 32, 1, false, sceGnmGpuMode());
	if( gerr != GPA_ERR_OK )
	{
		GNM_LOG( "color surface: %s\n", sceGpaStrError( gerr ));
		return false;
	}

	memset( &ci, 0, sizeof( ci ));
	ci.width = width;
	ci.height = height;
	ci.numslices = 1;
	ci.colorfmt = GNM_FMT_R8G8B8A8_UNORM;
	ci.colortilemodehint = props.tilemode;
	ci.mingpumode = sceGnmGpuMode();
	ci.numsamples = 1;
	ci.numfragments = 1;

	err = sceGnmCreateRenderTarget( rt, &ci );
	if( err == GNM_ERROR_OK )
		err = sceGnmRtCalcByteSize( &size, &align, rt );
	if( err != GNM_ERROR_OK )
	{
		GNM_LOG( "color target: %s\n", sceGnmStrError( err ));
		return false;
	}

	mem = gnm_dev_heap_alloc( size, align );
	if( !mem )
		return false;

	memset( mem, 0, size );
	sceGnmRtSetBaseAddr( rt, mem );
	return true;
}

static bool init_depth_target( GnmDepthRenderTarget *drt, int width, int height )
{
	const GnmDataFormat fmt = sceGnmDfInitFromZ( GNM_Z_32_FLOAT );
	GpaSurfaceProperties props;
	GnmDepthRenderTargetCreateInfo ci;
	uint64_t size = 0;
	uint32_t align = 0;
	GpaError gerr;
	GnmError err;
	void *mem;

	memset( &props, 0, sizeof( props ));
	gerr = sceGpaFindOptimalSurface( &props, GPA_SURFACE_DEPTH, sceGnmDfGetBitsPerElement( fmt ), 1, false, sceGnmGpuMode());
	if( gerr != GPA_ERR_OK )
	{
		GNM_LOG( "depth surface: %s\n", sceGpaStrError( gerr ));
		return false;
	}

	memset( &ci, 0, sizeof( ci ));
	ci.width = width;
	ci.height = height;
	ci.numslices = 1;
	ci.zfmt = GNM_Z_32_FLOAT;
	ci.stencilfmt = GNM_STENCIL_INVALID;
	ci.tilemodehint = props.tilemode;
	ci.mingpumode = sceGnmGpuMode();
	ci.numfragments = 1;

	err = sceGnmCreateDepthRenderTarget( drt, &ci );
	if( err == GNM_ERROR_OK )
		err = sceGnmDrtCalcByteSize( &size, &align, drt );
	if( err != GNM_ERROR_OK )
	{
		GNM_LOG( "depth target: %s\n", sceGnmStrError( err ));
		return false;
	}

	mem = gnm_dev_heap_alloc( size, align );
	if( !mem )
		return false;

	sceGnmDrtSetZReadAddress( drt, mem );
	sceGnmDrtSetZWriteAddress( drt, mem );
	return true;
}

static bool init_display( void )
{
	OrbisVideoOutResolutionStatus status;
	int i, res;

	res = sceVideoOutOpen( 0, ORBIS_VIDEO_OUT_BUS_MAIN, 0, NULL );
	if( res < 0 )
	{
		GNM_LOG( "VideoOutOpen failed: 0x%x\n", res );
		return false;
	}
	dev.videohandle = res;

	memset( &status, 0, sizeof( status ));
	res = sceVideoOutGetResolutionStatus( dev.videohandle, &status );
	if( res < 0 || !status.width || !status.height )
	{
		GNM_LOG( "GetResolutionStatus failed: 0x%x\n", res );
		return false;
	}

	gnm_dev.width = status.width;
	gnm_dev.height = status.height;

	if( sceKernelCreateEqueue( &dev.flipqueue, "gnm flip" ) < 0 || sceVideoOutAddFlipEvent( dev.flipqueue, dev.videohandle, NULL ) < 0 )
	{
		GNM_LOG( "flip queue setup failed\n" );
		return false;
	}

	for( i = 0; i < 2; i++ )
	{
		OrbisVideoOutBufferAttribute attr;
		void *addr;

		if( !init_color_target( &gnm_dev.color[i], gnm_dev.width, gnm_dev.height ))
			return false;

		// display reads the color target as is, values are sRGB encoded like on PC
		memset( &attr, 0, sizeof( attr ));
		sceVideoOutSetBufferAttribute( &attr, ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB, 0 /* tiled */,
			0 /* 16:9 */, gnm_dev.width, gnm_dev.height, sceGnmRtGetPitch( &gnm_dev.color[i] ));

		addr = sceGnmRtGetBaseAddr( &gnm_dev.color[i] );
		res = sceVideoOutRegisterBuffers( dev.videohandle, i, &addr, 1, &attr );
		if( res < 0 )
		{
			GNM_LOG( "RegisterBuffers failed: 0x%x\n", res );
			return false;
		}
	}

	sceVideoOutSetFlipRate( dev.videohandle, 0 ); // 60 Hz
	return init_depth_target( &gnm_dev.depth, gnm_dev.width, gnm_dev.height );
}

bool gnm_dev_init( void )
{
	int debug = gnm_dev.debug;
	bool stats_on = gnm_dev.stats;

	if( dev.initialized )
		return true;

	memset( &gnm_dev, 0, sizeof( gnm_dev ));
	gnm_dev.debug = debug;
	gnm_dev.stats = stats_on;

	dev.tables = map_garlic( TABLE_POOL_SIZE, 64 * 1024, 0x80000000ULL, true );
	if( !dev.tables )
	{
		GNM_LOG( "can't map descriptor table pool below 4 GB\n" );
		return false;
	}

	dev.transient = gnm_dev_heap_alloc( TRANSIENT_SIZE, 64 * 1024 );
	dev.cmdmem = gnm_dev_heap_alloc( CMD_SIZE, 256 );
	dev.label = gnm_dev_heap_alloc( 256, 256 );
	if( !dev.transient || !dev.cmdmem || !dev.label )
		return false;

	*dev.label = 0;

	if( !init_display( ))
		return false;

	GNM_LOG( "device ready: %dx%d, gpu mode %d, tables at %p\n", gnm_dev.width, gnm_dev.height, (int)sceGnmGpuMode(), dev.tables );

	dev.initialized = true;
	begin_cmd();
	return true;
}

void gnm_dev_shutdown( void )
{
	if( !dev.initialized )
		return;

	// wait for the GPU, then give the display back
	submit_and_wait( "shutdown" );
	sceGnmSubmitDone();

	sceVideoOutClose( dev.videohandle );
	sceKernelDeleteEqueue( dev.flipqueue );
	dev.initialized = false;
	// GPU memory stays mapped, it's reused if the renderer starts again
}

static void gnm_dev_log( const char *fmt, ... ) __attribute__(( format( printf, 1, 2 )));
static void gnm_dev_log( const char *fmt, ... )
{
	va_list va;

	va_start( va, fmt );
	vprintf( fmt, va );
	va_end( va );
}

const gnm_dev_api_t gnm_dev_api =
{
	.dev = &gnm_dev,
	.log = gnm_dev_log,
	.set_restart_callback = gnm_dev_set_restart_callback,
	.reserve = gnm_dev_reserve,
	.flush = gnm_dev_flush,
	.alloc_table = gnm_dev_alloc_table,
	.alloc_transient = gnm_dev_alloc_transient,
	.heap_alloc = gnm_dev_heap_alloc,
	.heap_free = gnm_dev_heap_free,
	.load_vs = gnm_dev_load_vs,
	.load_ps = gnm_dev_load_ps,
	.set_user_sgpr = gnm_dev_set_user_sgpr,
	.set_context_reg = gnm_dev_set_context_reg,
};

void gnm_dev_configure( int debug, bool stats, bool flip_front, int mipmaps )
{
	gnm_dev.debug = debug;
	gnm_dev.stats = stats;
	gnm_dev.flip_front = flip_front;
	gnm_dev.mipmaps = mipmaps;
}

void gnm_dev_get_size( int *width, int *height )
{
	*width = gnm_dev.width;
	*height = gnm_dev.height;
}

const void *gnm_dev_get_api( void )
{
	return &gnm_dev_api;
}
