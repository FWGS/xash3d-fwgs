/*
 * Copyright (C) 2008, 2009 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined __ANDROID__ && !defined XASH_64BIT
#include <string.h>
#include <android/log.h>
#include <dlfcn.h>
#include "linker.h"

static Elf_Sym* soinfo_elf_lookup( soinfo* si, unsigned hash, const char* name )
{
    Elf_Sym* symtab = si->symtab;
    const char* strtab = si->strtab;

	if( si->nbucket == 0 )
		return NULL;

	for( unsigned n = si->bucket[hash % si->nbucket]; n != 0; n = si->chain[n] )
	{
        Elf_Sym* s = symtab + n;
		if( strcmp( strtab + s->st_name, name )) continue;

            /* only concern ourselves with global and weak symbol definitions */
		switch( ELF_ST_BIND( s->st_info ))
		{
        case STB_GLOBAL:
        case STB_WEAK:
			if( s->st_shndx == SHN_UNDEF )
                continue;
            return s;
        }
    }

    return NULL;
}

static Elf_Sym* soinfo_elf_lookup_reverse(soinfo* si, size_t addr)
{
	Elf_Sym* symtab = si->symtab;

	if( si->nbucket == 0 )
		return NULL;

	for( int j = 0; j < si->nbucket; j++ )
	{
		for( unsigned n = si->bucket[j]; n != 0; n = si->chain[n] )
		{
			Elf_Sym* s = symtab + n;
			if( s->st_value != addr )continue;

				/* only concern ourselves with global and weak symbol definitions */
			switch( ELF_ST_BIND( s->st_info ))
			{
			case STB_GLOBAL:
			case STB_LOCAL:
			case STB_WEAK:
				if (s->st_shndx == SHN_UNDEF)
					continue;
				return s;
			}
		}
	}

	return NULL;
}


static unsigned elfhash( const char* _name )
{
	const unsigned char* name = ( const unsigned char* ) _name;
    unsigned h = 0, g;

	while(*name)
	{
        h = (h << 4) + *name++;
        g = h & 0xf0000000;
        h ^= g;
        h ^= g >> 24;
    }
    return h;
}

/* This is used by dlsym(3).  It performs symbol lookup only within the
   specified soinfo object and not in any of its dependencies.

   TODO: Only looking in the specified soinfo seems wrong. dlsym(3) says
   that it should do a breadth first search through the dependency
   tree. This agrees with the ELF spec (aka System V Application
   Binary Interface) where in Chapter 5 it discuss resolving "Shared
   Object Dependencies" in breadth first search order.
 */
static Elf_Sym* dlsym_handle_lookup( soinfo* si, const char* name )
{
	return soinfo_elf_lookup( si, elfhash( name ), name );
}
#define XASH_ANDROID 1
#include "lib_android.h"

static int dladdr_fallback( const void *addr, Dl_info *info )
{
	static soinfo *server_info;
	Elf_Sym *sym;

	if( !server_info )
		server_info = ( soinfo* )ANDROID_GetServerLibrary();
	if( !server_info )
		return 0;
	//__android_log_print( ANDROID_LOG_ERROR, "dladdr_fb", "%p %p\n", addr, server_info );

	sym =  soinfo_elf_lookup_reverse( server_info, ((char*)addr) - ((char*)server_info->base ));
	//__android_log_print( ANDROID_LOG_ERROR, "dladdr_fb", "sym %p %p\n", addr, sym );
	if( sym )
	{
		info->dli_sname = server_info->strtab + sym->st_name;
		info->dli_fname = "server";
		info->dli_fbase = (void*)server_info->base;
		info->dli_saddr = (void*)addr;
		//__android_log_print( ANDROID_LOG_ERROR, "dladdr_fb", "name %p %s\n", addr, info->dli_sname );
		return 1;
	}

	return 0;
}



extern "C" int __attribute__((visibility("hidden"))) dladdr( const void *addr, Dl_info *info )
{
	typedef int (*PFNDLADDR)( const void *addr, Dl_info *info );
	PFNDLADDR pfn_dladdr;

	if( !pfn_dladdr )
	{
		/* android does not have libdl, but have soinfo record for it from linker
		 * use dlopen to get this record directly */
		void *lib = dlopen( "libdl.so", RTLD_NOW );

		if( lib )
			pfn_dladdr = (PFNDLADDR)dlsym( lib, "dladdr" );
		if( pfn_dladdr == (PFNDLADDR)dladdr )
			pfn_dladdr = 0;
		if( !pfn_dladdr )
			pfn_dladdr = (PFNDLADDR)dladdr_fallback;
	}

	return pfn_dladdr( addr, info );
}


extern "C" void* dlsym_weak( void* handle, const char* symbol ) {

  soinfo* found = NULL;
  Elf_Sym* sym = NULL;
  found = reinterpret_cast<soinfo*>( handle );
  sym = dlsym_handle_lookup( found, symbol );

  if ( sym != NULL ) {
	  return reinterpret_cast<void*>( sym->st_value + found->base /*load_bias*/ );
  }
  __android_log_print( ANDROID_LOG_ERROR, "dlsym-weak", "Failed when looking up %s\n", symbol );
  return NULL;
}
#endif
