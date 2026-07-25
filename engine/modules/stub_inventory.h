/*
stub_inventory.h - Stub module inventory header for GameCube port
Copyright (C) 2026 xash3d-gc contributors

Header file for stub module inventory system.
*/

#ifndef STUB_INVENTORY_H
#define STUB_INVENTORY_H

#include "module.h"

// Initialize stub inventory
qboolean Stub_Inventory_Init(void);

// Shutdown stub inventory
void Stub_Inventory_Shutdown(void);

// Report stub inventory status
void Stub_Inventory_Report(void);

#endif /* STUB_INVENTORY_H */
