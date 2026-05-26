#include "gl_local.h"

extern "C"
{
extern void GetRefAPI( int version, ref_interface_t *funcs, ref_api_t *engfuncs, ref_globals_t *globals );

struct {const char *name;void *func;} lib_ref_gl_exports[] = {
{ "GetRefAPI", &GetRefAPI },
{ 0, 0 }
};
}