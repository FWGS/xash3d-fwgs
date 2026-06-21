/* Minimal GameCube apploader for locally generated Xash3D test discs. */

typedef unsigned int u32;

typedef struct apploader_section_s
{
	u32 offset;
	u32 address;
	u32 size;
} apploader_section_t;

#include "gamecube-apploader-sections.h"

typedef void (*report_fn)( const char *format, ... );
typedef void (*init_fn)( report_fn report );
typedef int (*main_fn)( void **address, u32 *size, u32 *offset );
typedef void *(*close_fn)( void );

static int section_index;

static void apploader_init( report_fn report )
{
	(void)report;
	section_index = 0;
}

static int apploader_main( void **address, u32 *size, u32 *offset )
{
	const apploader_section_t *section;

	if( section_index >= APPLOADER_SECTION_COUNT )
		return 0;

	section = &apploader_sections[section_index++];
	*address = (void *)section->address;
	*size = section->size;
	*offset = section->offset;
	return 1;
}

static void *apploader_close( void )
{
	*(volatile u32 *)0x80000034u = APPLOADER_FST_ADDRESS;
	*(volatile u32 *)0x80000038u = APPLOADER_FST_ADDRESS;
	*(volatile u32 *)0x8000003cu = APPLOADER_FST_SIZE;
	/* devkitPPC's __app_start clears SBSS/BSS before calling main. */
	return (void *)APPLOADER_ENTRY_POINT;
}

__attribute__((section(".text.start")))
void apploader_entry( init_fn *init, main_fn *main, close_fn *close )
{
	*init = apploader_init;
	*main = apploader_main;
	*close = apploader_close;
}
