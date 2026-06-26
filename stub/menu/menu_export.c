#include "common.h"
#include "menu_int.h"
#include "gamecube/dll_gamecube.h"

extern int EXPORT GetMenuAPI( UI_FUNCTIONS *pFunctionTable, ui_enginefuncs_t *engfuncs, ui_globalvars_t *pGlobals );

static dllexport_t gamecube_menu_exports[] =
{
	{ "GetMenuAPI", (void *)GetMenuAPI },
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
