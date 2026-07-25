/*
module.c - Module infrastructure implementation for GameCube port
Copyright (C) 2026 xash3d-gc contributors

Module system for GameCube port with stub implementations for
missing modules and fallback behavior.
*/

#include "module.h"
#include "common.h"
#include <stdlib.h>
#include <string.h>

#define MAX_MODULES 64

static module_inventory_t g_module_inventory;
static qboolean g_module_initialized = false;

// Initialize module inventory
qboolean Module_Init(void)
{
	if (g_module_initialized)
		return true;

	memset(&g_module_inventory, 0, sizeof(g_module_inventory));
	g_module_inventory.modules = (module_info_t *)malloc(sizeof(module_info_t) * MAX_MODULES);
	
	if (!g_module_inventory.modules)
	{
		Con_Reportf(S_ERROR "Module: Failed to allocate module inventory\n");
		return false;
	}

	g_module_inventory.module_count = 0;
	g_module_inventory.module_capacity = MAX_MODULES;
	g_module_inventory.initialized = true;
	g_module_initialized = true;

	Con_Reportf("Module: Inventory initialized with capacity %d modules\n", MAX_MODULES);
	return true;
}

// Shutdown module inventory
void Module_Shutdown(void)
{
	if (!g_module_initialized)
		return;

	// Unload all modules
	for (int i = 0; i < g_module_inventory.module_count; i++)
	{
		if (g_module_inventory.modules[i].handle)
		{
			// Module cleanup would go here
			Con_Reportf("Module: Unloading %s\n", g_module_inventory.modules[i].name);
		}
	}

	free(g_module_inventory.modules);
	g_module_inventory.modules = NULL;
	g_module_inventory.module_count = 0;
	g_module_inventory.module_capacity = 0;
	g_module_inventory.initialized = false;
	g_module_initialized = false;

	Con_Reportf("Module: Inventory shutdown complete\n");
}

// Get module inventory
module_inventory_t *Module_GetInventory(void)
{
	if (!g_module_initialized)
	{
		if (!Module_Init())
			return NULL;
	}
	return &g_module_inventory;
}

// Find module by name
module_info_t *Module_Find(const char *name)
{
	module_inventory_t *inv = Module_GetInventory();
	
	if (!inv || !name)
		return NULL;

	for (int i = 0; i < inv->module_count; i++)
	{
		if (inv->modules[i].name && Q_strcasecmp(inv->modules[i].name, name) == 0)
			return &inv->modules[i];
	}

	return NULL;
}

// Load module (stub implementation)
module_info_t *Module_Load(const char *name, module_type_t type)
{
	module_inventory_t *inv = Module_GetInventory();
	
	if (!inv || !name)
		return NULL;

	// Check if already loaded
	module_info_t *existing = Module_Find(name);
	if (existing)
	{
		existing->refcount++;
		return existing;
	}

	// Find empty slot
	if (inv->module_count >= inv->module_capacity)
	{
		Con_Reportf(S_ERROR "Module: Inventory full, cannot load %s\n", name);
		return NULL;
	}

	module_info_t *module = &inv->modules[inv->module_count];
	memset(module, 0, sizeof(module_info_t));

	module->name = strdup(name);
	module->type = type;
	module->state = MODULE_STATE_LOADED;
	module->refcount = 1;
	module->handle = NULL;  // Stub - no actual handle
	module->exports = NULL; // Stub - no exports

	inv->module_count++;

	Con_Reportf("Module: Loaded stub module %s (type %d)\n", name, type);
	return module;
}

// Unload module
qboolean Module_Unload(const char *name)
{
	module_info_t *module = Module_Find(name);
	
	if (!module)
	{
		Con_Reportf(S_WARN "Module: Cannot unload %s - not found\n", name);
		return false;
	}

	module->refcount--;
	
	if (module->refcount <= 0)
	{
		module->state = MODULE_STATE_UNLOADED;
		
		// Remove from inventory
		for (int i = 0; i < g_module_inventory.module_count; i++)
		{
			if (&g_module_inventory.modules[i] == module)
			{
				// Shift remaining modules
				for (int j = i; j < g_module_inventory.module_count - 1; j++)
				{
					g_module_inventory.modules[j] = g_module_inventory.modules[j + 1];
				}
				g_module_inventory.module_count--;
				break;
			}
		}

		if (module->name)
			free((void *)module->name);
	}

	return true;
}

// Report module status
void Module_Report(void)
{
	module_inventory_t *inv = Module_GetInventory();
	
	if (!inv)
		return;

	Con_Reportf("Module: Inventory report (%d/%d modules)\n", 
		inv->module_count, inv->module_capacity);

	for (int i = 0; i < inv->module_count; i++)
	{
		module_info_t *m = &inv->modules[i];
		const char *state_str = "UNKNOWN";
		
		switch (m->state)
		{
			case MODULE_STATE_UNLOADED: state_str = "UNLOADED"; break;
			case MODULE_STATE_LOADING:  state_str = "LOADING";  break;
			case MODULE_STATE_LOADED:   state_str = "LOADED";   break;
			case MODULE_STATE_ERROR:    state_str = "ERROR";    break;
		}

		Con_Reportf("  [%d] %s (type=%d, state=%s, refcount=%d)\n",
			i, m->name ? m->name : "unknown", m->type, state_str, m->refcount);
	}
}

// Register stub module
int Module_RegisterStub(const char *name, module_type_t type, const char *version, const char *description)
{
	module_inventory_t *inv = Module_GetInventory();
	
	if (!inv)
		return -1;

	// Check if already registered
	if (Module_Find(name))
	{
		Con_Reportf(S_WARN "Module: Stub %s already registered\n", name);
		return -2;
	}

	// Find empty slot
	if (inv->module_count >= inv->module_capacity)
	{
		Con_Reportf(S_ERROR "Module: Inventory full, cannot register %s\n", name);
		return -3;
	}

	module_info_t *module = &inv->modules[inv->module_count];
	memset(module, 0, sizeof(module_info_t));

	module->name = strdup(name);
	module->version = version ? strdup(version) : NULL;
	module->description = description ? strdup(description) : NULL;
	module->type = type;
	module->state = MODULE_STATE_UNLOADED;
	module->refcount = 0;
	module->handle = NULL;
	module->exports = NULL;

	inv->module_count++;

	Con_Reportf("Module: Registered stub %s (type=%d, version=%s)\n", 
		name, type, version ? version : "unknown");
	return 0;
}

// Create stub module with exports
int Module_CreateStub(const char *name, module_type_t type, const char *version, const char *description, void *exports)
{
	module_inventory_t *inv = Module_GetInventory();
	
	if (!inv)
		return -1;

	// Check if already registered
	if (Module_Find(name))
	{
		Con_Reportf(S_WARN "Module: Stub %s already exists\n", name);
		return -2;
	}

	// Find empty slot
	if (inv->module_count >= inv->module_capacity)
	{
		Con_Reportf(S_ERROR "Module: Inventory full, cannot create %s\n", name);
		return -3;
	}

	module_info_t *module = &inv->modules[inv->module_count];
	memset(module, 0, sizeof(module_info_t));

	module->name = strdup(name);
	module->version = version ? strdup(version) : NULL;
	module->description = description ? strdup(description) : NULL;
	module->type = type;
	module->state = MODULE_STATE_LOADED;
	module->refcount = 1;
	module->handle = NULL;
	module->exports = exports;

	inv->module_count++;

	Con_Reportf("Module: Created stub %s (type=%d, exports=%p)\n", 
		name, type, exports);
	return 0;
}
