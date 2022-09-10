#if 0
#include "build.h"
#if XASH_LINUX
#include <sys/inotify.h>
#include <errno.h>
#include <string.h>
#endif
#include "filesystem_internal.h"
#include "common/com_strings.h"

#define MAX_FS_WATCHES 256

struct
{
#if XASH_LINUX
	int fd;
	int count;
	struct
	{
		fs_event_callback_t callback;
		int fd;
	} watch[MAX_FS_WATCHES];
#endif // XASH_LINUX
} fsnotify;

#if XASH_LINUX
static qboolean FS_InotifyInit( void )
{
	int fd;

	if(( fd = inotify_init1( IN_NONBLOCK )) < 0 )
	{
		Con_Printf( S_ERROR "inotify_init1 failed: %s", strerror( errno ));
		return false;
	}

	fsnotify.fd = fd;
	return true;
}

static qboolean FS_InotifyWasInit( void )
{
	return fsnotify.fd >= 0;
}
#endif

/*
===============
FS_AddWatch

Adds on-disk path to filesystem watcher list
Every file modification will call back
===============
*/
int FS_AddWatch( const char *path, fs_event_callback_t callback )
{
#if XASH_LINUX
	int fd;
	const uint mask = IN_CREATE | IN_DELETE | IN_MODIFY;

	if( !FS_InotifyWasInit() && !FS_InotifyInit())
			return false;

	if(( fd = inotify_add_watch( fsnotify.fd, path, mask )) < 0 )
	{
		Con_Printf( S_ERROR "inotify_add_watch failed: %s", strerror( errno ));
		return false;
	}

	fsnotify.watch[fsnotify.count].fd = fd;
	fsnotify.watch[fsnotify.count].callback = callback;

	return true;
#else
	return false;
#endif
}

/*
===============
FS_WatchFrame

Polls any changes and runs call backs
===============
*/
void FS_WatchFrame( void )
{
#if XASH_LINUX
	int i;

	for( i = 0; i < fsnotify.count; i++ )
	{
		struct inotify_event events;
	}

#endif
}

/*
===============
FS_WatchInitialize

initializes filesystem watcher subsystem
===============
*/
qboolean FS_WatchInitialize( void )
{
#if XASH_LINUX
	fsnotify.fd = -1; // only call inotify init when requested
	fsnotify.count = 0;

	return true;
#else
	return false;
#endif
}
#endif // 0
