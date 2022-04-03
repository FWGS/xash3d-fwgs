/*
	From the vitaXash3D by fgsfdsfgs.
*/
#include "common.h"
#include "dll_psp.h"
#include <pspsdk.h>
#include <pspkernel.h>
#include <pspuser.h>

#define MAX_DLNAMELEN 256

#if 1
typedef struct table_s
{
	const char *name;
	void *pointer;
} table_t;

#include "generated_library_tables.h"
#endif

typedef struct dll_s
{
	SceUID handle;
	char name[MAX_DLNAMELEN];
	int refcnt;
	dllexport_t *exp;
	struct dll_s *next;
} dll_t;

static dll_t *dll_list;
static char *dll_err = NULL;
static char dll_err_buf[1024];

static void *dlfind( const char *name )
{
	dll_t *d = NULL;
	for( d = dll_list; d; d = d->next )
		if( !Q_strcmp( d->name, name ) )
			break;
	return d;
}

static const char *dlname( void *handle )
{
	dll_t *d = NULL;
	// iterate through all dll_ts to check if the handle is actually in the list
	// and not some bogus pointer from god knows where
	for( d = dll_list; d; d = d->next ) if( d == handle ) break;
	return d ? d->name : NULL;
}

void *dlopen( const char *name, int flag )
{
	dll_t	*old, *new;
	char	fullpath[MAX_SYSPATH];
	char	static_name[16];
	void	*static_exp;
	void	*dynamic_exp;
	void	*exp_p;
	SceUID	h;

	if( !name )
		return NULL;

	old = dlfind( name );
	if( old )
	{
		old->refcnt++;
		return old;
	}

	Q_snprintf( fullpath, sizeof( fullpath ), "%s", name );

	exp_p = &dynamic_exp;
	static_exp = NULL;
	memset( static_name, 0x00, sizeof( static_name ) );

	h = Platform_LoadModule( fullpath, 0, sizeof( exp_p ), &exp_p );
	if( h < 0 )
	{
		snprintf( dll_err_buf, sizeof( dll_err_buf ), "dlopen( %s ): error %#010x\n", name, h );
		dll_err = dll_err_buf;

		COM_FileBase( name, static_name );

		if( !Q_strncmp( static_name, "lib", 3 ) )
			Q_sprintf( static_name, "%s", &static_name[3] );

		table_t* tbl = ( table_t* )libs;
		while( tbl->name )
		{
			if( !Q_strcmp( tbl->name, static_name) )
			{
				static_exp = tbl->pointer;
				break;
			}
			tbl++;
		}

		if( static_exp == NULL )
			return NULL;

		Con_Printf ( "dlopen( %s ): ( static ) success!\n", fullpath );
	}
	else
	{
		Con_Printf ( "dlopen( %s ): ( dynamic ) success!\n", fullpath );
	}

	new = calloc( 1, sizeof( dll_t ) );
	if( !new )
	{
		dll_err = "dlopen(): out of memory";
		return NULL;
	}
	snprintf( new->name, MAX_DLNAMELEN, name );

	if( static_exp != NULL )
	{
		new->handle = -1;
		new->exp = static_exp;
	}
	else
	{
		new->handle = h;
		new->exp = dynamic_exp;
	}

	new->refcnt = 1;

	new->next = dll_list;
	dll_list = new;

	return new;
}

void *dlsym( void *handle, const char *symbol )
{
	if( !handle || !symbol )
	{
		dll_err = "dlsym(): NULL args";
		return NULL;
	}

	if( !dlname( handle ) )
	{
		dll_err = "dlsym(): unknown handle";
		return NULL;
	}

	dll_t *d = handle;
	if( !d->refcnt )
	{
		dll_err = "dlsym(): call dlopen() first";
		return NULL;
	}

	dllexport_t *f = NULL;
	for( f = d->exp; f && f->func; f++ )
	{
		if( !Q_strcmp( f->name, symbol ) )
			break;
	}

	if( f && f->func )
	{
		return f->func;
	}
	else
	{
		dll_err = "dlsym(): symbol not found in dll";
		return NULL;
	}
}

int dlclose( void *handle )
{
	int		result, sce_code;
	dll_t	*d;

	if( !handle )
	{
		dll_err = "dlclose(): NULL arg";
		return -1;
	}

	if( !dlname( handle ) )
	{
		dll_err = "dlclose(): unknown handle";
		return -2;
	}

	d = handle;
	d->refcnt--;
	if( d->refcnt <= 0 )
	{
		if( d->handle != -1 )
		{
			result = Platform_UnloadModule( d->handle, &sce_code );
			if( result < 0 )
			{
				if( result == -1 )
				{
					dll_err = "dlclose(): module doesn't want to stop";
					return -3;
				}

				snprintf( dll_err_buf, sizeof( dll_err_buf ), "dlclose( %s ): error %#010x", d->name, sce_code );
				dll_err = dll_err_buf;
			}
		}

		if( d == dll_list )
			dll_list = NULL;
		else
		{
			dll_t *pd;
			for( pd = dll_list; pd; pd = pd->next )
			{
				if( pd->next == d )
				{
					pd->next = d->next;
					break;
				}
			}
		}
		free( d );
	}
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
	dll_t *d = NULL;
	dllexport_t *f = NULL;
	for( d = dll_list; d; d = d->next )
		for( f = d->exp; f && f->func; f++ )
			if( f->func == addr ) goto for_end;
for_end:
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
