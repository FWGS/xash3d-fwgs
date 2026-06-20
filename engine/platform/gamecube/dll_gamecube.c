/*
dll_gamecube.c - static library registration for GameCube (from xash3d-wii)
*/
#include "crtlib.h"
#include "dll_gamecube.h"

#if XASH_GAMECUBE
#include "common.h"
#include "library.h"
#include "filesystem.h"
#endif

typedef struct dll_s
{
	const char *name;
	int refcnt;
	dllexport_t *exp;
	struct dll_s *next;
} dll_t;

static dll_t *dll_list;
static char *dll_err = NULL;

static void *dlfind( const char *name )
{
	dll_t *d;

	for( d = dll_list; d; d = d->next )
		if( !Q_strcmp( d->name, name ))
			break;
	return d;
}

static const char *dlname( void *handle )
{
	dll_t *d;

	for( d = dll_list; d; d = d->next )
		if( d == handle )
			break;
	return d ? d->name : NULL;
}

void *dlopen( const char *name, int flag )
{
	dll_t *d = dlfind( name );
	(void)flag;

	if( d )
		d->refcnt++;
	else
		dll_err = "dlopen(): unknown dll name";
	return d;
}

void *dlsym( void *handle, const char *symbol )
{
	dll_t *d = handle;
	dllexport_t *f;

	if( !handle || !symbol )
	{
		dll_err = "dlsym(): NULL args";
		return NULL;
	}

	if( !dlname( handle ))
	{
		dll_err = "dlsym(): unknown handle";
		return NULL;
	}

	d = handle;
	if( !d->refcnt )
	{
		dll_err = "dlsym(): call dlopen() first";
		return NULL;
	}

	for( f = d->exp; f && f->func; f++ )
		if( !Q_strcmp( f->name, symbol ))
			break;

	if( f && f->func )
		return f->func;

	dll_err = "dlsym(): symbol not found in dll";
	return NULL;
}

int dlclose( void *handle )
{
	dll_t *d = handle;

	if( !handle )
	{
		dll_err = "dlclose(): NULL arg";
		return -1;
	}

	if( !dlname( handle ))
	{
		dll_err = "dlclose(): unknown handle";
		return -2;
	}

	if( !d->refcnt )
	{
		dll_err = "dlclose(): call dlopen() first";
		return -3;
	}

	d->refcnt--;
	return 0;
}

char *dlerror( void )
{
	char *err = dll_err;
	dll_err = NULL;
	return err;
}

int dladdr( const void *addr, Dl_info *info )
{
	dll_t *d;
	dllexport_t *f;

	for( d = dll_list; d; d = d->next )
	{
		for( f = d->exp; f && f->func; f++ )
			if( f->func == addr )
				goto found;
	}

found:
	if( d && f && f->func )
	{
		if( info )
		{
			info->dli_fhandle = d;
			info->dli_sname = f->name;
			info->dli_saddr = addr;
		}
		return 1;
	}
	return 0;
}

int dll_register( const char *name, dllexport_t *exports )
{
	dll_t *entry;

	if( !name || !exports )
		return -1;
	if( dlfind( name ))
		return -2;

	entry = calloc( 1, sizeof( dll_t ));
	if( !entry )
		return -3;

	entry->name = name;
	entry->exp = exports;
	entry->next = dll_list;
	dll_list = entry;
	return 0;
}

int setup_gamecube_dll_functions( void )
{
	extern int setup_gamecube_ref_exports( void );
	return setup_gamecube_ref_exports();
}

#if XASH_GAMECUBE

void *COM_LoadLibrary( const char *dllname, int build_ordinals_table, qboolean directpath )
{
	dll_user_t *hInst;

	(void)build_ordinals_table;
	COM_ResetLibraryError();

	hInst = FS_FindLibrary( dllname, directpath );
	if( !hInst )
		return dlopen( dllname, 0 );

	if( hInst->custom_loader )
	{
		Mem_Free( hInst );
		return NULL;
	}

	if( !hInst->hInstance )
		hInst->hInstance = dlopen( hInst->fullPath, 0 );

	{
		void *handle = hInst->hInstance;
		Mem_Free( hInst );
		return handle;
	}
}

void COM_FreeLibrary( void *hInstance )
{
	if( hInstance )
		dlclose( hInstance );
}

void *COM_GetProcAddress( void *hInstance, const char *name )
{
	return dlsym( hInstance, name );
}

void *COM_FunctionFromName( void *hInstance, const char *pName )
{
	return COM_GetProcAddress( hInstance, pName );
}

const char *COM_NameForFunction( void *hInstance, void *function )
{
	Dl_info info = { 0 };

	(void)hInstance;

	if( dladdr( function, &info ) && info.dli_sname )
		return info.dli_sname;

	return NULL;
}
#endif /* XASH_GAMECUBE */
