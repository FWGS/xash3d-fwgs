/*
ps4_crtlib.c - startup code for PS4 modules (.prx)
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
Replacement for crtlib.o from OpenOrbis toolchain, which defines its own
__init_array_start/__init_array_end variables instead of using the ones
provided by the linker, so global constructors of a module are never run.

Constructors are run from module_start and from exported __ps4_module_init,
which the engine calls right after loading a module, whatever happens first.
*/

#include <stdint.h>

// module parameters, same as in the original crtlib
__asm__(
	".pushsection \".data.sce_module_param\", \"aw\"\n"
	".align 8\n"
	"_sceModuleParam:\n"
	"	.quad 0x18\n"        // size
	"	.quad 0x13C13F4BF\n" // magic
	"	.quad 0x1000051\n"   // SDK version
	".popsection\n"
);

void *__dso_handle = &__dso_handle;
void *_sceLibc = 0;

// defined by the linker around .init_array output section
extern void ( *__init_array_start[] )( void ) __attribute__(( weak, visibility( "hidden" )));
extern void ( *__init_array_end[] )( void ) __attribute__(( weak, visibility( "hidden" )));

static int ps4_module_initialized;

__attribute__(( visibility( "default" ))) void __ps4_module_init( void )
{
	if( ps4_module_initialized )
		return;

	ps4_module_initialized = 1;

	for( void ( **i )( void ) = __init_array_start; i != __init_array_end; i++ )
		( *i )( );
}

__attribute__(( visibility( "hidden" ))) int32_t module_start( int64_t args, const void *argp )
{
	(void)args;
	(void)argp;

	__ps4_module_init( );
	return 0;
}

__attribute__(( visibility( "hidden" ))) int32_t module_stop( int64_t args, const void *argp )
{
	(void)args;
	(void)argp;
	return 0;
}

int32_t _init( void )
{
	return 0;
}

int32_t _fini( void )
{
	return 0;
}
