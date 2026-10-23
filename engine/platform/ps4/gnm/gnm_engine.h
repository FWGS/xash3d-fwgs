/*
gnm_engine.h - GNM device as the engine sees it
Copyright (C) 2026 sergiopoverony

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef GNM_ENGINE_H
#define GNM_ENGINE_H

#include <stdbool.h>

// Nothing from opengnm here: the engine is built without its headers (opengnm has its own
// platform.h), the device itself is described in gnm_dev.h for the code that uses opengnm.

// debug: 0 - quiet, 1 - log submits and texture uploads, 2 - also every draw; stats: frame timing in the log
void gnm_dev_configure( int debug, bool stats );
bool gnm_dev_init( void );
void gnm_dev_shutdown( void );
void gnm_dev_get_size( int *width, int *height );
// submits the frame, shows it and starts the next one
void gnm_dev_present( void );
// gnm_dev_api_t for the renderer
const void *gnm_dev_get_api( void );

#endif // GNM_ENGINE_H
