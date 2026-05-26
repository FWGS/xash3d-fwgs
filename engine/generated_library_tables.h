

extern table_t lib_filesystem_stdio_exports[];
extern table_t lib_hl_exports[]; //hl_dll
extern table_t lib_ref_soft_exports[];
extern table_t lib_cl_dll_exports[]; //cl_dll
extern table_t lib_menu_exports[];

struct {const char *name;void *func;} libs[] = {
{ "filesystem_stdio", &lib_filesystem_stdio_exports },
{ "ref_soft", &lib_ref_soft_exports },
{ "server", &lib_hl_exports },
{ "client", &lib_cl_dll_exports },
{ "menu", &lib_menu_exports},
/*
{ "server", &lib_hl_exports },
{ "ref_gl", &lib_ref_gl_exports },
{ "menu", &lib_menu_exports},
*/
//{ 0 }
};
