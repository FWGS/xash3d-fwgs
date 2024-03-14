#include <string.h>
#include "common.h"

extern movie_interface_t g_api;
movie_api_t g_engfuncs;

int EXPORT GetMovieAPI( int version, movie_api_t *api, movie_interface_t *interface );
int EXPORT GetMovieAPI( int version, movie_api_t *api, movie_interface_t *interface )
{
	if( version != MOVIE_API_VERSION )
		return 0;

	memcpy( &g_engfuncs, api, sizeof( g_engfuncs ));
	memcpy( interface, &g_api, sizeof( *interface ));

	return MOVIE_API_VERSION;
}
