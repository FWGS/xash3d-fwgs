/*
ps4_malloc.c - process-wide memory allocator for PS4 images
Copyright (C) 2026 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

/*
Every PS4 image (eboot.bin and each .prx) links its own static copy of
OpenOrbis musl with its own heap. Memory is passed between engine modules
all the time, and musl heap of a module got corrupted in practice.

The linker redirects malloc family here with --wrap (see scripts/waifulib/ps4.py).
eboot.bin owns the only heap: a simple size class allocator over mmap with
headers that detect invalid frees. It publishes a function table marked
with a magic signature in its data segment. Modules (built with PS4_MODULE)
find the table by scanning eboot.bin segments and forward everything there.
*/

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>

// engine logger, only present in eboot.bin
extern void PS4_Log( const char *fmt, ... ) __attribute__(( weak ));

typedef struct
{
	char magic[16];
	void *( *malloc )( size_t size );
	void ( *free )( void *ptr );
	void *( *calloc )( size_t num, size_t size );
	void *( *realloc )( void *ptr, size_t size );
	void *( *memalign )( size_t align, size_t size );
	size_t ( *usable_size )( void *ptr );
} ps4_heap_api_t;

// split in two, so the full signature exists only in the table itself
#define PS4_HEAP_MAGIC_A "XASH-PS4-"
#define PS4_HEAP_MAGIC_B "HEAP-1"

static const ps4_heap_api_t *heap;

#if !PS4_MODULE
/*
=============================================================================

	ALLOCATOR (eboot.bin only)

=============================================================================
*/
#define HEADER_MAGIC_USED  0x55534544u // 'USED'
#define HEADER_MAGIC_FREE  0x46524545u // 'FREE'
#define HEADER_ALIGNED     0x414c4e44u // 'ALND', points to real header

#define ALIGNMENT       16
#define NUM_CLASSES     48
#define MAX_SMALL       ( 256 * 1024 )
#define ARENA_SIZE      ( 16 * 1024 * 1024 )
#define PAGE            16384

typedef struct header_s
{
	uint32_t magic;
	uint32_t klass;     // size class, or NUM_CLASSES for large blocks
	size_t   size;      // usable size
	union
	{
		struct header_s *next; // free list link
		void *base;            // large block mapping
		struct header_s *real; // for aligned blocks
	};
	size_t   mapsize;   // large block mapping size
} header_t;

_Static_assert( sizeof( header_t ) % ALIGNMENT == 0, "header must keep alignment" );

static header_t *free_lists[NUM_CLASSES];
static size_t class_size[NUM_CLASSES];
static char *arena_cur, *arena_end;
static int heap_ready;

static volatile int heap_lock;
static volatile uintptr_t heap_owner;
static int heap_depth;

static inline uintptr_t ThreadId( void )
{
	uintptr_t self;

	// thread control block pointer, unique for each thread, no imports needed
	__asm__ volatile( "movq %%fs:0, %0" : "=r"( self ));
	return self;
}

static void Lock( void )
{
	uintptr_t self = ThreadId( );

	if( __atomic_load_n( &heap_owner, __ATOMIC_RELAXED ) == self )
	{
		heap_depth++;
		return;
	}

	while( __atomic_exchange_n( &heap_lock, 1, __ATOMIC_ACQUIRE ))
	{
		while( __atomic_load_n( &heap_lock, __ATOMIC_RELAXED ))
			__builtin_ia32_pause( );
	}

	__atomic_store_n( &heap_owner, self, __ATOMIC_RELAXED );
	heap_depth = 1;
}

static void Unlock( void )
{
	if( --heap_depth > 0 )
		return;

	__atomic_store_n( &heap_owner, 0, __ATOMIC_RELAXED );
	__atomic_store_n( &heap_lock, 0, __ATOMIC_RELEASE );
}

static void InitClasses( void )
{
	size_t size = ALIGNMENT;

	// 16, 32, 48, 64, then growing by ~1/4 up to MAX_SMALL
	for( int i = 0; i < NUM_CLASSES; i++ )
	{
		class_size[i] = size;

		if( size < 64 )
			size += 16;
		else
			size = ( size + size / 4 + ALIGNMENT - 1 ) & ~(size_t)( ALIGNMENT - 1 );

		if( size > MAX_SMALL )
			size = MAX_SMALL;
	}

	class_size[NUM_CLASSES - 1] = MAX_SMALL;
	heap_ready = 1;
}

static int SizeToClass( size_t size )
{
	for( int i = 0; i < NUM_CLASSES; i++ )
	{
		if( size <= class_size[i] )
			return i;
	}

	return NUM_CLASSES;
}

static void *MapMemory( size_t size )
{
	void *p = mmap( NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0 );

	return p == MAP_FAILED ? NULL : p;
}

static void Corrupted( const char *what, void *ptr, const header_t *h )
{
	if( PS4_Log )
		PS4_Log( "heap: %s %p (magic 0x%08x class %u size %zu)\n", what, ptr, h->magic, h->klass, h->size );

	__builtin_trap( );
}

static void *Alloc( size_t size )
{
	header_t *h;
	int klass;

	if( !heap_ready )
		InitClasses( );

	if( size == 0 )
		size = 1;

	if( size > (size_t)-1 / 2 )
	{
		errno = ENOMEM;
		return NULL;
	}

	klass = SizeToClass( size );

	if( klass == NUM_CLASSES )
	{
		size_t mapsize = ( size + sizeof( header_t ) + PAGE - 1 ) & ~(size_t)( PAGE - 1 );
		void *base = MapMemory( mapsize );

		if( !base )
		{
			errno = ENOMEM;
			return NULL;
		}

		h = base;
		h->magic = HEADER_MAGIC_USED;
		h->klass = NUM_CLASSES;
		h->size = mapsize - sizeof( header_t );
		h->base = base;
		h->mapsize = mapsize;
		return h + 1;
	}

	if(( h = free_lists[klass] ))
	{
		if( h->magic != HEADER_MAGIC_FREE || h->klass != (uint32_t)klass )
			Corrupted( "free list corrupted at", h + 1, h );

		free_lists[klass] = h->next;
	}
	else
	{
		size_t need = sizeof( header_t ) + class_size[klass];

		if( !arena_cur || (size_t)( arena_end - arena_cur ) < need )
		{
			// rest of the old arena is abandoned, it's at most MAX_SMALL
			char *arena = MapMemory( ARENA_SIZE );

			if( !arena )
			{
				errno = ENOMEM;
				return NULL;
			}

			arena_cur = arena;
			arena_end = arena + ARENA_SIZE;
		}

		h = (header_t *)arena_cur;
		arena_cur += need;
	}

	h->magic = HEADER_MAGIC_USED;
	h->klass = klass;
	h->size = class_size[klass];
	h->next = NULL;
	h->mapsize = 0;
	return h + 1;
}

static header_t *GetHeader( void *ptr )
{
	header_t *h = (header_t *)ptr - 1;

	if( h->magic == HEADER_ALIGNED )
		h = h->real;

	return h;
}

static void Free( void *ptr )
{
	header_t *h;

	if( !ptr )
		return;

	h = GetHeader( ptr );

	if( h->magic == HEADER_MAGIC_FREE )
		Corrupted( "double free of", ptr, h );

	if( h->magic != HEADER_MAGIC_USED )
		Corrupted( "invalid free of", ptr, h );

	if( h->klass == NUM_CLASSES )
	{
		h->magic = 0;
		munmap( h->base, h->mapsize );
		return;
	}

	if( h->klass > NUM_CLASSES )
		Corrupted( "invalid free of", ptr, h );

	h->magic = HEADER_MAGIC_FREE;
	h->next = free_lists[h->klass];
	free_lists[h->klass] = h;
}

static size_t UsableSize( void *ptr )
{
	header_t *h;

	if( !ptr )
		return 0;

	h = (header_t *)ptr - 1;

	if( h->magic == HEADER_ALIGNED )
	{
		header_t *real = h->real;
		return real->size - (size_t)( (char *)ptr - (char *)( real + 1 ));
	}

	if( h->magic != HEADER_MAGIC_USED )
		Corrupted( "usable size of invalid", ptr, h );

	return h->size;
}

static void *AllocAligned( size_t align, size_t size )
{
	char *raw, *aligned;
	header_t *marker;

	if( align <= ALIGNMENT )
		return Alloc( size );

	if( align & ( align - 1 ))
	{
		errno = EINVAL;
		return NULL;
	}

	raw = Alloc( size + align + sizeof( header_t ));
	if( !raw )
		return NULL;

	aligned = (char *)(( (uintptr_t)raw + sizeof( header_t ) + align - 1 ) & ~(uintptr_t)( align - 1 ));
	marker = (header_t *)aligned - 1;
	marker->magic = HEADER_ALIGNED;
	marker->klass = 0;
	marker->size = 0;
	marker->real = (header_t *)raw - 1;
	marker->mapsize = 0;

	return aligned;
}

static void *Realloc( void *ptr, size_t size )
{
	size_t old;
	void *p;

	if( !ptr )
		return Alloc( size );

	if( !size )
	{
		Free( ptr );
		return NULL;
	}

	old = UsableSize( ptr );

	// aligned blocks are always moved to keep things simple
	if( size <= old && ((header_t *)ptr - 1 )->magic == HEADER_MAGIC_USED )
		return ptr;

	if( !( p = Alloc( size )))
		return NULL;

	memcpy( p, ptr, old < size ? old : size );
	Free( ptr );
	return p;
}

static void *Heap_Malloc( size_t size )
{
	void *p;
	Lock( );
	p = Alloc( size );
	Unlock( );
	return p;
}

static void Heap_Free( void *ptr )
{
	Lock( );
	Free( ptr );
	Unlock( );
}

static void *Heap_Calloc( size_t num, size_t size )
{
	void *p;

	if( size && num > (size_t)-1 / size )
	{
		errno = ENOMEM;
		return NULL;
	}

	Lock( );
	p = Alloc( num * size );
	Unlock( );

	if( p )
		memset( p, 0, num * size );

	return p;
}

static void *Heap_Realloc( void *ptr, size_t size )
{
	void *p;
	Lock( );
	p = Realloc( ptr, size );
	Unlock( );
	return p;
}

static void *Heap_Memalign( size_t align, size_t size )
{
	void *p;
	Lock( );
	p = AllocAligned( align, size );
	Unlock( );
	return p;
}

static size_t Heap_UsableSize( void *ptr )
{
	size_t s;
	Lock( );
	s = UsableSize( ptr );
	Unlock( );
	return s;
}

// referenced from code below, so the linker keeps it; modules find it by the magic
__attribute__(( used, aligned( 16 ))) const ps4_heap_api_t ps4_heap_api =
{
	PS4_HEAP_MAGIC_A PS4_HEAP_MAGIC_B,
	Heap_Malloc,
	Heap_Free,
	Heap_Calloc,
	Heap_Realloc,
	Heap_Memalign,
	Heap_UsableSize,
};

static void FindHeap( void )
{
	heap = &ps4_heap_api;

	if( PS4_Log )
		PS4_Log( "heap: init\n" );
}

#else // PS4_MODULE
/*
=============================================================================

	HEAP LOOKUP (modules)

=============================================================================
*/
typedef struct
{
	size_t   size;
	char     name[256];
	struct { void *address; uint32_t size; int32_t prot; } segments[4];
	uint32_t segment_count;
	uint8_t  fingerprint[20];
} ps4_module_info_t;

int32_t sceKernelGetModuleList( uint32_t *array, size_t size, size_t *available );
int32_t sceKernelGetModuleInfo( uint32_t handle, ps4_module_info_t *info );

static void FindHeap( void )
{
	static const char magic_b[] = PS4_HEAP_MAGIC_B;
	uint32_t handles[256];
	size_t count = 0;

	if( sceKernelGetModuleList( handles, 256, &count ) < 0 )
		__builtin_trap( );

	for( size_t i = 0; i < count && i < 256; i++ )
	{
		ps4_module_info_t info;

		memset( &info, 0, sizeof( info ));
		info.size = sizeof( info );

		if( sceKernelGetModuleInfo( handles[i], &info ) < 0 || strcmp( info.name, "eboot.bin" ))
			continue;

		for( uint32_t s = 0; s < info.segment_count && s < 4; s++ )
		{
			const char *p = info.segments[s].address;
			const char *end = p + info.segments[s].size;

			// readable data segments only, code may be execute-only
			if( !( info.segments[s].prot & 1 ) || ( info.segments[s].prot & 4 ))
				continue;

			for( ; p + sizeof( ps4_heap_api_t ) <= end; p += 16 )
			{
				if( memcmp( p, PS4_HEAP_MAGIC_A, sizeof( PS4_HEAP_MAGIC_A ) - 1 ))
					continue;

				if( memcmp( p + sizeof( PS4_HEAP_MAGIC_A ) - 1, magic_b, sizeof( magic_b )))
					continue;

				heap = (const ps4_heap_api_t *)p;
				return;
			}
		}
	}

	// without shared heap modules would corrupt memory, better fail early
	__builtin_trap( );
}
#endif // PS4_MODULE

/*
=============================================================================

	WRAPPERS

=============================================================================
*/
#define HEAP() do { if( !heap ) FindHeap( ); } while( 0 )

// for diagnostics: 1 if this image uses heap of eboot.bin
__attribute__(( visibility( "default" ), used )) int __ps4_heap_shared( void )
{
	HEAP( );
	return 1;
}

void *__wrap_malloc( size_t size ) { HEAP( ); return heap->malloc( size ); }
void __wrap_free( void *ptr ) { if( !ptr ) return; HEAP( ); heap->free( ptr ); }
void *__wrap_calloc( size_t num, size_t size ) { HEAP( ); return heap->calloc( num, size ); }
void *__wrap_realloc( void *ptr, size_t size ) { HEAP( ); return heap->realloc( ptr, size ); }
void *__wrap_memalign( size_t align, size_t size ) { HEAP( ); return heap->memalign( align, size ); }
void *__wrap_aligned_alloc( size_t align, size_t size ) { HEAP( ); return heap->memalign( align, size ); }
void *__wrap_valloc( size_t size ) { HEAP( ); return heap->memalign( 16384, size ); }
size_t __wrap_malloc_usable_size( void *ptr ) { HEAP( ); return heap->usable_size( ptr ); }

int __wrap_posix_memalign( void **ptr, size_t align, size_t size )
{
	void *p;

	if( align < sizeof( void * ) || ( align & ( align - 1 )))
		return EINVAL;

	HEAP( );

	if( !( p = heap->memalign( align, size )))
		return ENOMEM;

	*ptr = p;
	return 0;
}
