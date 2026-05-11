/*
sys_psvita.c - psvita backend
Copyright (C) 2021-2023 fgsfds

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "platform/platform.h"
#include "xash3d_mathlib.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <vitasdk.h>
#include <vitaGL.h>
#include <vrtld.h>
#include <sys/reent.h>

#define DATA_PATH "data/xash3d"
#define MAX_ARGV 5 // "" -log -dev X NULL

// 200MB libc heap, 512K main thread stack, 40MB for loading game DLLs
// the rest goes to vitaGL
SceUInt32 sceUserMainThreadStackSize = 512 * 1024;
unsigned int _pthread_stack_default_user = 512 * 1024;
unsigned int _newlib_heap_size_user = 200 * 1024 * 1024;
#define VGL_MEM_THRESHOLD ( 40 * 1024 * 1024 )

// HACKHACK: create some slack at the end of the RX segment of the ELF
// for vita-elf-create to put the generated symbol table into
const char vitaelf_slack __attribute__ ((aligned (0x20000))) = 0xFF;

/* HACKHACK: force-export stuff required by the dynamic libs */

extern void *__aeabi_idiv;
extern void *__aeabi_uidiv;
extern void *__aeabi_idivmod;
extern void *__aeabi_uidivmod;
extern void *__aeabi_d2ulz;
extern void *__aeabi_ul2d;
extern void *__aeabi_ul2f;

static const vrtld_export_t aux_exports[] =
{
	VRTLD_EXPORT_SYMBOL( __aeabi_d2ulz ),
	VRTLD_EXPORT_SYMBOL( __aeabi_idiv ),
	VRTLD_EXPORT_SYMBOL( __aeabi_idivmod ),
	VRTLD_EXPORT_SYMBOL( __aeabi_uidivmod ),
	VRTLD_EXPORT_SYMBOL( __aeabi_uidiv ),
	VRTLD_EXPORT_SYMBOL( __aeabi_ul2d ),
	VRTLD_EXPORT_SYMBOL( __aeabi_ul2f ),
	VRTLD_EXPORT_SYMBOL( _impure_ptr ),
	VRTLD_EXPORT_SYMBOL( ctime ),
	VRTLD_EXPORT_SYMBOL( vasprintf ),
	VRTLD_EXPORT_SYMBOL( vsprintf ),
	VRTLD_EXPORT_SYMBOL( vprintf ),
	VRTLD_EXPORT_SYMBOL( printf ),
	VRTLD_EXPORT_SYMBOL( putchar ),
	VRTLD_EXPORT_SYMBOL( puts ),
	VRTLD_EXPORT_SYMBOL( tolower ),
	VRTLD_EXPORT_SYMBOL( toupper ),
	VRTLD_EXPORT_SYMBOL( isalnum ),
	VRTLD_EXPORT_SYMBOL( isalpha ),
	VRTLD_EXPORT_SYMBOL( strchrnul ),
	VRTLD_EXPORT_SYMBOL( strtok ),
	VRTLD_EXPORT_SYMBOL( stpcpy ),
	VRTLD_EXPORT_SYMBOL( rand ),
	VRTLD_EXPORT_SYMBOL( srand ),
	VRTLD_EXPORT_SYMBOL( rintf ),
	VRTLD_EXPORT_SYMBOL( sceGxmMapMemory ), // needed by vgl_shim
	VRTLD_EXPORT( "dlopen", vrtld_dlopen ),
	VRTLD_EXPORT( "dlclose", vrtld_dlclose ),
	VRTLD_EXPORT( "dlsym", vrtld_dlsym ),
};

const vrtld_export_t *__vrtld_exports = aux_exports;
const size_t __vrtld_num_exports = sizeof( aux_exports ) / sizeof( *aux_exports );

/* end of export crap */

static const char *PSVita_GetLaunchParameter( char *outbuf )
{
	SceAppUtilAppEventParam param;
	memset( &param, 0, sizeof( param ) );
	sceAppUtilReceiveAppEvent( &param );
	if( param.type == 0x05 )
	{
		sceAppUtilAppEventParseLiveArea( &param, outbuf );
		return outbuf;
	}
	return NULL;
}

void Platform_ShellExecute( const char *path, const char *parms )
{
	Con_Reportf( S_WARN "Tried to shell execute ;%s; -- not supported\n", path );
}

/*
===========
PSVita_GetArgv

On the PS Vita under normal circumstances argv is empty, so we'll construct our own
based on which button the user pressed in the LiveArea launcher.
===========
*/
int PSVita_GetArgv( int in_argc, char **in_argv, char ***out_argv )
{
	static const char *fake_argv[MAX_ARGV] = { "app0:/eboot.bin", NULL };
	int fake_argc = 1;
	char tmp[2048] = { 0 };
	SceAppUtilInitParam initParam = { 0 };
	SceAppUtilBootParam bootParam = { 0 };

	// on the Vita under normal circumstances argv is empty, unless we're launching from Change Game
	sceAppUtilInit( &initParam, &bootParam );

	if( in_argc > 1 )
	{
		// probably coming from Change Game, in which case we just need to keep the old args
		*out_argv = in_argv;
		return in_argc;
	}

	// got empty args, which means that we're probably coming from LiveArea
	// construct argv based on which button the user pressed in the LiveArea launcher
	if( PSVita_GetLaunchParameter( tmp ))
	{
		if( !Q_strcmp( tmp, "dev" ))
		{
			// user hit the "Developer Mode" button, inject "-log" and "-dev" arguments
			fake_argv[fake_argc++] = "-log";
			fake_argv[fake_argc++] = "-dev";
			fake_argv[fake_argc++] = "2";
		}
	}

	*out_argv = (char **)fake_argv;
	return fake_argc;
}

void PSVita_Init( void )
{
	char xashdir[1024] = { 0 };

	// cd to the base dir immediately for library loading to work
	if( PSVita_GetBasePath( xashdir, sizeof( xashdir )))
	{
		chdir( xashdir );
	}

	sceTouchSetSamplingState( SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_STOP );
	scePowerSetArmClockFrequency( 444 );
	scePowerSetBusClockFrequency( 222 );
	scePowerSetGpuClockFrequency( 222 );
	scePowerSetGpuXbarClockFrequency( 166 );
	sceSysmoduleLoadModule( SCE_SYSMODULE_NET );

	if( vrtld_init( 0 ) < 0 )
	{
		Sys_Error( "Could not init vrtld:\n%s\n", vrtld_dlerror() );
	}

	// init vitaGL, leaving some memory for DLL mapping
	// TODO: we don't need to do this for ref_soft
	vglUseVram( GL_TRUE );
	vglUseExtraMem( GL_TRUE );
	vglInitExtended( 0, 960, 544, VGL_MEM_THRESHOLD, 0 );
}

void PSVita_Shutdown( void )
{
	vrtld_quit( );
}

qboolean PSVita_GetBasePath( char *buf, const size_t buflen )
{
	// check if a xash3d folder exists on one of these drives
	// default to the last one (ux0)
	static const char *drives[] = { "uma0", "imc0", "ux0" };
	SceUID dir;
	size_t i;

	for ( i = 0; i < sizeof( drives ) / sizeof( *drives ); ++i )
	{
		Q_snprintf( buf, buflen, "%s:" DATA_PATH, drives[i] );
		dir = sceIoDopen( buf );
		if ( dir >= 0 )
		{
			sceIoDclose( dir );
			return true;
		}
	}

	return false;
}

int PSVita_GetPSID( char *buf, const size_t buflen )
{
	SceKernelOpenPsId id;
	const int datasize = Q_min( buflen, sizeof( id ));

	if( sceKernelGetOpenPsId( &id ) < 0 )
	{
		return 0;
	}
	else
	{
		memcpy( buf, &id.id[0], datasize );
		return datasize;
	}
}
