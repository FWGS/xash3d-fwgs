#include "common.h"
#include "menu_int.h"
#include "gamecube/dll_gamecube.h"

// Forward declarations
int EXPORT GetMenuAPI( UI_FUNCTIONS *pFunctionTable, ui_enginefuncs_t* engfuncs, ui_globalvars_t *pGlobals );
int EXPORT GetExtAPI( int version, UI_EXTENDED_FUNCTIONS *pFunctionTable, ui_extendedfuncs_t *engfuncs );

static dllexport_t gamecube_menu_exports[] =
{
	{ "GetMenuAPI", (void *)GetMenuAPI },
	{ "GetExtAPI", (void *)GetExtAPI },
	{ NULL, NULL },
};

int setup_gamecube_menu_exports( void )
{
	int ret = 0;

	ret |= dll_register( "menu", gamecube_menu_exports );
	ret |= dll_register( "menu.so", gamecube_menu_exports );
	ret |= dll_register( "libmenu.so", gamecube_menu_exports );

	return ret;
}
