// basic typedefs
#ifndef XASH_TYPES_H
#define XASH_TYPES_H

#include <wchar.h> // off_t
#include <sys/types.h> // off_t
#include <stdint.h>
#include <assert.h>
#include "build.h"
#include "port.h"

#define MAX_STRING    256  // generic string
#define MAX_VA_STRING 1024 // compatibility macro
#define MAX_SYSPATH   1024 // system filepath
#define MAX_OSPATH    260 // max length of a filesystem pathname
#define CS_SIZE       64  // size of one config string
#define CS_TIME	      16  // size of time string

// platform-specific alignment for types, to not break ABI
#if XASH_PSP
	#define MAYBE_ALIGNED( x ) __attribute__(( aligned( 16 )))
#endif

#if !defined( MAYBE_ALIGNED )
	#define MAYBE_ALIGNED( x )
#endif // !defined( MAYBE_ALIGNED )

typedef uint8_t  byte;
typedef float    vec_t;
typedef vec_t    vec2_t[2];
#ifndef vec3_t // SDK renames it to Vector
typedef vec_t    vec3_t[3];
#endif
typedef vec_t    vec4_t[4] MAYBE_ALIGNED( 16 );
typedef vec_t    quat_t[4] MAYBE_ALIGNED( 16 );
typedef byte     rgba_t[4]; // unsigned byte colorpack
typedef byte     rgb_t[3];  // unsigned byte colorpack
typedef vec_t    matrix3x4[3][4] MAYBE_ALIGNED( 16 );
typedef vec_t    matrix4x4[4][4] MAYBE_ALIGNED( 16 );
typedef uint32_t poolhandle_t;
typedef uint32_t dword;
typedef char     string[MAX_STRING];
typedef off_t    fs_offset_t;
#if XASH_WIN32
typedef int      fs_size_t; // return type of _read, _write funcs
#else // !XASH_WIN32
typedef ssize_t  fs_size_t;
#endif // !XASH_WIN32
typedef unsigned int uint;
typedef void *(*pfnCreateInterface_t)( const char *, int * );

#undef true
#undef false

// true and false are keywords in C++ and C23
#if !__cplusplus &&  __STDC_VERSION__ < 202311L
enum { false, true };
#endif
typedef int qboolean;

#if XASH_LOW_MEMORY == 1 || XASH_PSP
	#define MAX_QPATH 48
	#define MAX_MODS  16
#elif XASH_LOW_MEMORY == 2
	#define MAX_QPATH 32 // should be enough for singleplayer
	#define MAX_MODS  4
#else // !XASH_LOW_MEMORY
	#define MAX_QPATH 64
	#define MAX_MODS  512
#endif // !XASH_LOW_MEMORY

#define BIT( n )   ( 1U << ( n ))
#define BIT64( n ) ( 1ULL << ( n ))

#define SetBits( bit_vector, bits )   (( bit_vector ) |= ( bits ))
#define ClearBits( bit_vector, bits ) (( bit_vector ) &= ~( bits ))
#define FBitSet( bit_vector, bits )   (( bit_vector ) & ( bits ))

// color strings
#define IsColorString( p ) (( p ) && *( p ) == '^' && *(( p ) + 1) && *(( p ) + 1) >= '0' && *(( p ) + 1 ) <= '9' )
#define ColorIndex( c )	   ((( c ) - '0' ) & 7 )

#if defined( __GNUC__ )
	#if defined( __i386__ )
		#define EXPORT         __attribute__(( visibility( "default" ), force_align_arg_pointer ))
		#define GAME_EXPORT    __attribute__(( force_align_arg_pointer ))
	#else // !defined( __i386__ )
		#define EXPORT __attribute__(( visibility ( "default" )))
	#endif // !defined( __i386__ )

	#if __GNUC__ >= 11
		// might want to set noclone due to https://gcc.gnu.org/bugzilla/show_bug.cgi?id=116893
		// but it's easier to not force mismatched-dealloc to error yet
		#define MALLOC_LIKE( x, y ) __attribute__(( malloc( x, y )))
	#else
		#define MALLOC_LIKE( x, y ) __attribute__(( malloc ))
	#endif

	#define RETURNS_NONNULL    __attribute__(( returns_nonnull ))
	#if !__clang__ && !__MCST__
		// clang has bugged returns_nonnull for functions pointers, it's ignored and generates a warning about objective-c? O_o
		// lcc doesn't support it at all
		#define PFN_RETURNS_NONNULL RETURNS_NONNULL
	#endif
	#define NORETURN           __attribute__(( noreturn ))
	#define NONNULL            __attribute__(( nonnull ))
	#define FORMAT_CHECK( x )  __attribute__(( format( printf, x, x + 1 )))
	#define ALLOC_CHECK( x )   __attribute__(( alloc_size( x )))
	#define WARN_UNUSED_RESULT __attribute__(( warn_unused_result ))
	#define RENAME_SYMBOL( x ) asm( x )
#elif defined( _MSC_VER )
	#define EXPORT __declspec( dllexport )
#endif

#if defined( __SANITIZE_ADDRESS__ )
	#define USE_ASAN 1

	#if defined( __GNUC__ )
		#define NO_ASAN __attribute__(( no_sanitize( "address" )))
	#elif defined( _MSC_VER )
		#define NO_ASAN __declspec( no_sanitize_address )
	#endif
#endif

#if __GNUC__ >= 3
	#define unlikely( x )     __builtin_expect( x, 0 )
	#define likely( x )       __builtin_expect( x, 1 )
#elif defined( __has_builtin )
	#if __has_builtin( __builtin_expect ) // this must be after defined() check
		#define unlikely( x )     __builtin_expect( x, 0 )
		#define likely( x )       __builtin_expect( x, 1 )
	#endif
#endif

#if !defined( __cplusplus ) && __STDC_VERSION__ >= 199101L // not C++ and C99 or newer
	#define XASH_RESTRICT restrict
#elif _MSC_VER || __GNUC__ || __clang__ // compiler-specific extensions
	#define XASH_RESTRICT __restrict
#endif

#if !defined( EXPORT )
	#define EXPORT
#endif // !defined( EXPORT )

#if !defined( GAME_EXPORT )
	#define GAME_EXPORT
#endif // !defined( GAME_EXPORT )

#if !defined( MALLOC_LIKE )
	#define MALLOC_LIKE( x, y )
#endif // !defined MALLOC_LIKE

#if !defined( NO_ASAN )
	#define NO_ASAN
#endif // !defined( NO_ASAN )

#if !defined( RETURNS_NONNULL )
	#define RETURNS_NONNULL
#endif // !defined( RETURNS_NONNULL )

#if !defined( PFN_RETURNS_NONNULL )
	#define PFN_RETURNS_NONNULL
#endif // !defined( PFN_RETURNS_NONNULL )

#if !defined( NORETURN )
        #define NORETURN
#endif // !defined( NORETURN )

#if !defined( NONNULL )
        #define NONNULL
#endif // !defined( NONNULL )

#if !defined( FORMAT_CHECK )
        #define FORMAT_CHECK( x )
#endif // !defined( FORMAT_CHECK )

#if !defined( ALLOC_CHECK )
        #define ALLOC_CHECK( x )
#endif // !defined( ALLOC_CHECK )

#if !defined( WARN_UNUSED_RESULT )
        #define WARN_UNUSED_RESULT
#endif // !defined( WARN_UNUSED_RESULT )

#if !defined( RENAME_SYMBOL )
        #define RENAME_SYMBOL( x )
#endif // !defined( RENAME_SYMBOL )

#if !defined( unlikely ) || !defined( likely )
	#define unlikely( x ) ( x )
	#define likely( x )   ( x )
#endif // !defined( unlikely ) || !defined( likely )

#if !defined( XASH_RESTRICT )
	#define XASH_RESTRICT
#endif

#if __STDC_VERSION__ >= 202311L || __cplusplus >= 201103L // C23 or C++ static_assert is a keyword
	#define STATIC_ASSERT_( ignore, x, y ) static_assert( x, y )
	#define STATIC_ASSERT  static_assert
#elif __STDC_VERSION__ >= 201112L // in C11 it's _Static_assert
	#define STATIC_ASSERT_( ignore, x, y ) _Static_assert( x, y )
	#define STATIC_ASSERT  _Static_assert
#else
	#define STATIC_ASSERT_( id, x, y ) extern int id[( x ) ? 1 : -1]
	// need these to correctly expand the line macro
	#define STATIC_ASSERT_3( line, x, y ) STATIC_ASSERT_( static_assert_ ## line, x, y )
	#define STATIC_ASSERT_2( line, x, y ) STATIC_ASSERT_3( line, x, y )
	#define STATIC_ASSERT( x, y ) STATIC_ASSERT_2( __LINE__, x, y )
#endif

// at least, statically check size of some public structures
#if XASH_64BIT
	#define STATIC_CHECK_SIZEOF( type, size32, size64 ) \
		STATIC_ASSERT( sizeof( type ) == size64, #type " unexpected size" )
#else
	#define STATIC_CHECK_SIZEOF( type, size32, size64 ) \
		STATIC_ASSERT( sizeof( type ) == size32, #type " unexpected size" )
#endif

#define Swap32( x ) (((uint32_t)((( x ) & 255 ) << 24 )) + ((uint32_t)(((( x ) >> 8 ) & 255 ) << 16 )) + ((uint32_t)((( x ) >> 16 ) & 255 ) << 8 ) + ((( x ) >> 24 ) & 255 ))
#define Swap16( x ) ((uint16_t)((((uint16_t)( x ) >> 8 ) & 255 ) + (((uint16_t)( x ) & 255 ) << 8 )))
#define Swap32Store( x ) ( x = Swap32( x ))
#define Swap16Store( x ) ( x = Swap16( x ))

#ifdef XASH_BIG_ENDIAN
	#define LittleLong( x )    Swap32( x )
	#define LittleShort( x )   Swap16( x )
	#define LittleLongSW( x )  Swap32Store( x )
	#define LittleShortSW( x ) Swap16Store( x )
	#define LittleFloat( x )   SwapFloat( x )
	#define BigLong( x )  ( x )
	#define BigShort( x ) ( x )
	#define BigFloat( x ) ( x )
#else
	#define LittleLong( x )  ( x )
	#define LittleShort( x )  ( x )
	#define LittleFloat( x )  ( x )
	#define LittleLongSW( x )
	#define LittleShortSW( x )
	#define BigLong( x )  Swap32( x )
	#define BigShort( x ) Swap16( x )
	#define BigFloat( x ) SwapFloat( x )
#endif

#endif // XASH_TYPES_H
