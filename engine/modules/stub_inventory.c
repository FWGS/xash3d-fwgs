/*
stub_inventory.c - Stub module inventory for GameCube port
Copyright (C) 2026 xash3d-gc contributors

This file provides stub implementations for modules that are not yet
implemented for GameCube. These stubs allow the engine to run with
reduced functionality while maintaining compatibility.
*/

#include "module.h"
#include "common.h"
#include <stdlib.h>
#include <string.h>

// Stub module exports - placeholder function pointers
typedef struct stub_exports_s
{
	void *client_api;
	void *server_api;
	void *menu_api;
	void *ref_api;
	void *fs_api;
	void *audio_api;
	void *input_api;
} stub_exports_t;

static stub_exports_t g_stub_exports;

// Stub module initialization functions (return success but do nothing)
static int stub_init_client(void) { return 1; }
static int stub_init_server(void) { return 1; }
static int stub_init_menu(void) { return 1; }
static int stub_init_ref(void) { return 1; }
static int stub_init_fs(void) { return 1; }
static int stub_init_audio(void) { return 1; }
static int stub_init_input(void) { return 1; }

// Stub module shutdown functions (do nothing)
static void stub_shutdown_client(void) {}
static void stub_shutdown_server(void) {}
static void stub_shutdown_menu(void) {}
static void stub_shutdown_ref(void) {}
static void stub_shutdown_fs(void) {}
static void stub_shutdown_audio(void) {}
static void stub_shutdown_input(void) {}

// Stub module query functions
static const char *stub_query_client(const char *query) { return "client_stub"; }
static const char *stub_query_server(const char *query) { return "server_stub"; }
static const char *stub_query_menu(const char *query) { return "menu_stub"; }
static const char *stub_query_ref(const char *query) { return "ref_stub"; }
static const char *stub_query_fs(const char *query) { return "fs_stub"; }
static const char *stub_query_audio(const char *query) { return "audio_stub"; }
static const char *stub_query_input(const char *query) { return "input_stub"; }

// Stub module API structures
static module_api_t g_stub_client_api = {
	.init = stub_init_client,
	.shutdown = stub_shutdown_client,
	.query = stub_query_client
};

static module_api_t g_stub_server_api = {
	.init = stub_init_server,
	.shutdown = stub_shutdown_server,
	.query = stub_query_server
};

static module_api_t g_stub_menu_api = {
	.init = stub_init_menu,
	.shutdown = stub_shutdown_menu,
	.query = stub_query_menu
};

static module_api_t g_stub_ref_api = {
	.init = stub_init_ref,
	.shutdown = stub_shutdown_ref,
	.query = stub_query_ref
};

static module_api_t g_stub_fs_api = {
	.init = stub_init_fs,
	.shutdown = stub_shutdown_fs,
	.query = stub_query_fs
};

static module_api_t g_stub_audio_api = {
	.init = stub_init_audio,
	.shutdown = stub_shutdown_audio,
	.query = stub_query_audio
};

static module_api_t g_stub_input_api = {
	.init = stub_init_input,
	.shutdown = stub_shutdown_input,
	.query = stub_query_input
};

// Initialize stub inventory
qboolean Stub_Inventory_Init(void)
{
	if (!Module_Init())
		return false;

	// Register stub modules
	Module_RegisterStub("client", MODULE_TYPE_CLIENT, "1.0.0-stub", "Client stub module");
	Module_RegisterStub("server", MODULE_TYPE_SERVER, "1.0.0-stub", "Server stub module");
	Module_RegisterStub("menu", MODULE_TYPE_MENU, "1.0.0-stub", "Menu stub module");
	Module_RegisterStub("ref", MODULE_TYPE_REF, "1.0.0-stub", "Renderer stub module");
	Module_RegisterStub("filesystem_stdio", MODULE_TYPE_FS, "1.0.0-stub", "Filesystem stub module");
	Module_RegisterStub("audio", MODULE_TYPE_AUDIO, "1.0.0-stub", "Audio stub module");
	Module_RegisterStub("input", MODULE_TYPE_INPUT, "1.0.0-stub", "Input stub module");

	// Create stub modules with exports
	Module_CreateStub("client", MODULE_TYPE_CLIENT, "1.0.0-stub", "Client stub module", &g_stub_client_api);
	Module_CreateStub("server", MODULE_TYPE_SERVER, "1.0.0-stub", "Server stub module", &g_stub_server_api);
	Module_CreateStub("menu", MODULE_TYPE_MENU, "1.0.0-stub", "Menu stub module", &g_stub_menu_api);
	Module_CreateStub("ref", MODULE_TYPE_REF, "1.0.0-stub", "Renderer stub module", &g_stub_ref_api);
	Module_CreateStub("filesystem_stdio", MODULE_TYPE_FS, "1.0.0-stub", "Filesystem stub module", &g_stub_fs_api);
	Module_CreateStub("audio", MODULE_TYPE_AUDIO, "1.0.0-stub", "Audio stub module", &g_stub_audio_api);
	Module_CreateStub("input", MODULE_TYPE_INPUT, "1.0.0-stub", "Input stub module", &g_stub_input_api);

	Con_Reportf("Stub: Inventory initialized with %d stub modules\n", Module_GetInventory()->module_count);
	return true;
}

// Shutdown stub inventory
void Stub_Inventory_Shutdown(void)
{
	Module_Shutdown();
	Con_Reportf("Stub: Inventory shutdown complete\n");
}

// Report stub inventory status
void Stub_Inventory_Report(void)
{
	Module_Report();
}
