// basic typedefs
#ifndef XASH_TYPES_H
#define XASH_TYPES_H

#include "build.h"

#if XASH_IRIX
#include <port.h>
#endif

#if XASH_WIN32
#include <wchar.h> // off_t
#endif // _WIN32

#include <sys/types.h> // off_t
#include STDINT_H
#include <assert.h>

typedef unsigned char byte;
typedef int		sound_t;
typedef float		vec_t;
typedef vec_t		vec2_t[2];
typedef vec_t		vec3_t[3];
typedef vec_t		vec4_t[4];
typedef vec_t		quat_t[4];
typedef byte		rgba_t[4];	// unsigned byte colorpack
typedef byte		rgb_t[3];		// unsigned byte colorpack
typedef vec_t		matrix3x4[3][4];
typedef vec_t		matrix4x4[4][4];

#if XASH_64BIT
typedef uint32_t        poolhandle_t;
#else
typedef void*           poolhandle_t;
#endif

#undef true
#undef false

#ifndef __cplusplus
typedef enum { false, true }	qboolean;
#else
typedef int qboolean;
#endif

typedef uint64_t longtime_t;

#define MAX_STRING		256	// generic string
#define MAX_INFO_STRING	256	// infostrings are transmitted across network
#define MAX_SERVERINFO_STRING	512	// server handles too many settings. expand to 1024?
#define MAX_LOCALINFO_STRING	32768	// localinfo used on server and not sended to the clients
#define MAX_SYSPATH		1024	// system filepath
#define MAX_PRINT_MSG	8192	// how many symbols can handle single call of Con_Printf or Con_DPrintf
#define MAX_TOKEN		2048	// parse token length
#define MAX_MODS		512	// environment games that engine can keep visible
#define MAX_USERMSG_LENGTH	2048	// don't modify it's relies on a client-side definitions

#define BIT( n )		( 1U << ( n ))
#define GAMMA		( 2.2f )		// Valve Software gamma
#define INVGAMMA		( 1.0f / 2.2f )	// back to 1.0
#define TEXGAMMA		( 0.9f )		// compensate dim textures
#define SetBits( iBitVector, bits )	((iBitVector) = (iBitVector) | (bits))
#define ClearBits( iBitVector, bits )	((iBitVector) = (iBitVector) & ~(bits))
#define FBitSet( iBitVector, bit )	((iBitVector) & (bit))

#ifndef __cplusplus
#ifdef NULL
#undef NULL
#endif

#define NULL		((void *)0)
#endif

// color strings
#define IsColorString( p )	( p && *( p ) == '^' && *(( p ) + 1) && *(( p ) + 1) >= '0' && *(( p ) + 1 ) <= '9' )
#define ColorIndex( c )	((( c ) - '0' ) & 7 )

#if defined(__GNUC__)
	#ifdef __i386__
		#define EXPORT __attribute__ ((visibility ("default"),force_align_arg_pointer))
		#define GAME_EXPORT __attribute((force_align_arg_pointer))
	#else
		#define EXPORT __attribute__ ((visibility ("default")))
		#define GAME_EXPORT
	#endif
	#define _format(x) __attribute__((format(printf, x, x+1)))
	#define NORETURN __attribute__((noreturn))
#elif defined(_MSC_VER)
	#define EXPORT          __declspec( dllexport )
	#define GAME_EXPORT
	#define _format(x)
	#define NORETURN
#else
	#define EXPORT
	#define GAME_EXPORT
	#define _format(x)
	#define NORETURN
#endif

#if ( __GNUC__ >= 3 )
	#define unlikely(x) __builtin_expect(x, 0)
	#define likely(x)   __builtin_expect(x, 1)
#elif defined( __has_builtin )
	#if __has_builtin( __builtin_expect )
		#define unlikely(x) __builtin_expect(x, 0)
		#define likely(x)   __builtin_expect(x, 1)
	#else
		#define unlikely(x) (x)
		#define likely(x)   (x)
	#endif
#else
	#define unlikely(x) (x)
	#define likely(x)   (x)
#endif

#if defined( static_assert ) // C11 static_assert
#define STATIC_ASSERT static_assert
#else
#define STATIC_ASSERT( x, y ) extern int _static_assert_##__LINE__[( x ) ? 1 : -1]
#endif

#ifdef XASH_BIG_ENDIAN
#define LittleLong(x) (((int)(((x)&255)<<24)) + ((int)((((x)>>8)&255)<<16)) + ((int)(((x)>>16)&255)<<8) + (((x) >> 24)&255))
#define LittleLongSW(x) (x = LittleLong(x) )
#define LittleShort(x) ((short)( (((short)(x) >> 8) & 255) + (((short)(x) & 255) << 8)))
#define LittleShortSW(x) (x = LittleShort(x) )
_inline float LittleFloat( float f )
{
	union
	{
		float f;
		unsigned char b[4];
	} dat1, dat2;

	dat1.f = f;
	dat2.b[0] = dat1.b[3];
	dat2.b[1] = dat1.b[2];
	dat2.b[2] = dat1.b[1];
	dat2.b[3] = dat1.b[0];

	return dat2.f;
}
#else
#define LittleLong(x) (x)
#define LittleLongSW(x)
#define LittleShort(x) (x)
#define LittleShortSW(x)
#define LittleFloat(x) (x)
#endif


typedef unsigned int	dword;
typedef unsigned int	uint;
typedef char		string[MAX_STRING];
typedef struct file_s	file_t;		// normal file
typedef struct stream_s	stream_t;		// sound stream for background music playing
typedef off_t fs_offset_t;
#if XASH_WIN32
typedef int fs_size_t; // return type of _read, _write funcs
#else /* !XASH_WIN32 */
typedef ssize_t fs_size_t;
#endif /* !XASH_WIN32 */

typedef struct dllfunc_s
{
	const char	*name;
	void		**func;
} dllfunc_t;

typedef struct dll_info_s
{
	const char	*name;	// name of library
	const dllfunc_t	*fcts;	// list of dll exports
	qboolean		crash;	// crash if dll not found
	void		*link;	// hinstance of loading library
} dll_info_t;

typedef void (*setpair_t)( const char *key, const void *value, const void *buffer, void *numpairs );

// config strings are a general means of communication from
// the server to all connected clients.
// each config string can be at most CS_SIZE characters.
#if XASH_LOW_MEMORY == 0
#define MAX_QPATH		64	// max length of a game pathname
#elif XASH_LOW_MEMORY == 2
#define MAX_QPATH		32 // should be enough for singleplayer
#elif XASH_LOW_MEMORY == 1
#define MAX_QPATH 48
#endif
#define MAX_OSPATH		260	// max length of a filesystem pathname
#define CS_SIZE		64	// size of one config string
#define CS_TIME		16	// size of time string

#endif // XASH_TYPES_H
