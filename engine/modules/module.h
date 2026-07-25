/*
module.h - Module infrastructure for GameCube port
Copyright (C) 2026 xash3d-gc contributors

Module system for GameCube port with stub implementations for
missing modules and fallback behavior.
*/

#ifndef MODULE_H
#define MODULE_H

#include "common.h"

// Module types
typedef enum module_type_e
{
	MODULE_TYPE_UNKNOWN = 0,
	MODULE_TYPE_CLIENT,
	MODULE_TYPE_SERVER,
	MODULE_TYPE_MENU,
	MODULE_TYPE_REF,
	MODULE_TYPE_FS,
	MODULE_TYPE_AUDIO,
	MODULE_TYPE_INPUT,
	MODULE_TYPE_MAX
} module_type_t;

// Module state
typedef enum module_state_e
{
	MODULE_STATE_UNLOADED = 0,
	MODULE_STATE_LOADING,
	MODULE_STATE_LOADED,
	MODULE_STATE_ERROR,
	MODULE_STATE_MAX
} module_state_t;

// Module information structure
typedef struct module_info_s
{
	const char *name;           // Module name
	const char *version;        // Module version
	const char *description;    // Module description
	module_type_t type;         // Module type
	module_state_t state;       // Current state
	int refcount;               // Reference count
	void *handle;               // Module handle (dlopen)
	void *exports;              // Exported functions
} module_info_t;

// Module export function pointer
typedef void (*module_export_func_t)(void);

// Module initialization function
typedef int (*module_init_func_t)(void);

// Module shutdown function
typedef void (*module_shutdown_func_t)(void);

// Module query function
typedef const char* (*module_query_func_t)(const char *query);

// Module API structure
typedef struct module_api_s
{
	module_init_func_t init;           // Initialize module
	module_shutdown_func_t shutdown;   // Shutdown module
	module_query_func_t query;         // Query module info
} module_api_t;

// Module registration structure
typedef struct module_registration_s
{
	const char *name;                 // Module name
	module_type_t type;               // Module type
	const char *version;              // Version string
	const char *description;          // Description
	module_api_t *api;                // Module API functions
	qboolean required;                // Is this module required?
} module_registration_t;

// Module inventory structure
typedef struct module_inventory_s
{
	module_info_t *modules;           // Array of module info
	int module_count;                 // Number of modules
	int module_capacity;              // Maximum number of modules
	qboolean initialized;             // Inventory initialized
} module_inventory_t;

// Function declarations
module_inventory_t *Module_GetInventory(void);
module_info_t *Module_Find(const char *name);
module_info_t *Module_Load(const char *name, module_type_t type);
qboolean Module_Unload(const char *name);
qboolean Module_Init(void);
void Module_Shutdown(void);
void Module_Report(void);

// Stub module functions
int Module_RegisterStub(const char *name, module_type_t type, const char *version, const char *description);
int Module_CreateStub(const char *name, module_type_t type, const char *version, const char *description, void *exports);

#endif /* MODULE_H */
