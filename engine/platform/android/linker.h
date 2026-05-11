/*
 * Copyright (C) 2008 The Android Open Source Project
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

#ifndef _LINKER_H_
#define _LINKER_H_

#include <unistd.h>
#include <sys/types.h>
#include <stdbool.h>
#include <linux/elf.h>

// a1ba: we don't really need custom linker on Android 64, because
// it's only intended to workaround bug which persist on Android < 4.4
#define Elf_Ehdr  Elf32_Ehdr
#define Elf_Phdr  Elf32_Phdr
#define Elf_Shdr  Elf32_Shdr
#define Elf_Sym   Elf32_Sym
#define Elf_Rel   Elf32_Rel
#define Elf_RelA  Elf32_Rela
#define Elf_Dyn   Elf32_Dyn
#define Elf_Half  Elf32_Half
#define Elf_Word  Elf32_Word
#define Elf_Sword Elf32_Sword
#define Elf_Addr  Elf32_Addr
#define Elf_Off   Elf32_Off
#define Elf_Nhdr  Elf32_Nhdr
#define Elf_Note  Elf32_Note

// Returns the address of the page containing address 'x'.
#define PAGE_START( x ) (( x )&PAGE_MASK )

// Returns the offset of address 'x' in its page.
#define PAGE_OFFSET( x ) (( x ) & ~PAGE_MASK )

// Returns the address of the next page after address 'x', unless 'x' is
// itself at the start of a page.
#define PAGE_END( x ) PAGE_START(( x ) + ( PAGE_SIZE - 1 ))

// Magic shared structures that GDB knows about.

struct link_map_t {
	uintptr_t  l_addr;
	char       *l_name;
	uintptr_t  l_ld;
	struct link_map_t *l_next;
	struct link_map_t *l_prev;
};

// Values for r_debug->state
enum {
	RT_CONSISTENT,
	RT_ADD,
	RT_DELETE
};
#define FLAG_LINKED 0x00000001
#define FLAG_EXE    0x00000004     // The main executable
#define FLAG_LINKER 0x00000010     // The linker itself

#define SOINFO_NAME_LEN 128

typedef void (*linker_function_t)( void );

// Android uses REL for 32-bit but only uses RELA for 64-bit.
#if defined( __LP64__ )
#define USE_RELA 1
#endif

struct soinfo {
	char           name[SOINFO_NAME_LEN];
	const Elf_Phdr *phdr;
	size_t         phnum;
	Elf_Addr       entry;
	Elf_Addr       base;
	unsigned       size;

#ifndef __LP64__
	uint32_t       unused1; // DO NOT USE, maintained for compatibility.
#endif

	Elf_Dyn        *dynamic;

#ifndef __LP64__
	uint32_t       unused2; // DO NOT USE, maintained for compatibility
	uint32_t       unused3; // DO NOT USE, maintained for compatibility
#endif

	struct soinfo  *next;
	unsigned       flags;

	const char     *strtab;
	Elf_Sym        *symtab;

	size_t         nbucket;
	size_t         nchain;
	unsigned       *bucket;
	unsigned       *chain;

#if !defined( __LP64__ )
	// This is only used by 32-bit MIPS, but needs to be here for
	// all 32-bit architectures to preserve binary compatibility.
	unsigned *plt_got;
#endif

#if defined( USE_RELA )
	Elf_RelA          *plt_rela;
	size_t            plt_rela_count;

	Elf_RelA          *rela;
	size_t            rela_count;
#else
	Elf_Rel           *plt_rel;
	size_t            plt_rel_count;

	Elf_Rel           *rel;
	size_t            rel_count;
#endif

	linker_function_t *preinit_array;
	size_t            preinit_array_count;

	linker_function_t *init_array;
	size_t            init_array_count;
	linker_function_t *fini_array;
	size_t            fini_array_count;

	linker_function_t init_func;
	linker_function_t fini_func;

#if defined( __arm__ )
	// ARM EABI section used for stack unwinding.
	unsigned   *ARM_exidx;
	size_t     ARM_exidx_count;
#elif defined( __mips__ )
	unsigned   mips_symtabno;
	unsigned   mips_local_gotno;
	unsigned   mips_gotsym;
#endif

	size_t            ref_count;
	struct link_map_t link_map;

	bool       constructors_called;

	// When you read a virtual address from the ELF file, add this
	// value to get the corresponding address in the process' address space.
	Elf_Addr load_bias;

#if !defined( __LP64__ )
	bool     has_text_relocations;
#endif
	bool     has_DT_SYMBOLIC;
};

#endif
