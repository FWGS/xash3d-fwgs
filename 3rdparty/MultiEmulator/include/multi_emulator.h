#ifndef MULTI_EMULATOR_H
#define MULTI_EMULATOR_H

#if defined( __GNUC__ )
	#if defined( __i386__ )
		#define ME_EXPORT         __attribute__(( visibility( "default" ), force_align_arg_pointer ))
	#else
		#define ME_EXPORT         __attribute__(( visibility ( "default" )))
	#endif
#else
	#if defined( _MSC_VER )
		#define ME_EXPORT         __declspec( dllexport )
	#else
		#define ME_EXPORT
	#endif
#endif

#include <string.h>

#if __cplusplus
extern "C"
{
#endif

int ME_EXPORT GenerateRevEmu2013( void *pDest, int nSteamID );
int ME_EXPORT GenerateSC2009( void *pDest, int nSteamID );
int ME_EXPORT GenerateOldRevEmu( void *pDest, int nSteamID );
int ME_EXPORT GenerateSteamEmu( void *pDest, int nSteamID );
int ME_EXPORT GenerateRevEmu( void *pDest, int nSteamID );
int ME_EXPORT GenerateSetti( void *pDest );
int ME_EXPORT GenerateAVSMP( void *pDest, int nSteamID, int bUniverse );

#if __cplusplus
}
#endif

#endif // MULTI_EMULATOR_H
