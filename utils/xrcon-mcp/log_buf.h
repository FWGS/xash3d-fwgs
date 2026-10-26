/*
log_buf.h - bounded line-oriented ring buffer for xrcon-mcp
Copyright (C) 2026 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef XRCON_MCP_LOG_BUF_H
#define XRCON_MCP_LOG_BUF_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
	char *data;
	size_t cap;
	size_t len;
	uint64_t total_appended;
	uint64_t total_evicted;
} log_buf_t;

int LogBuf_Init( log_buf_t *lb, size_t cap );
void LogBuf_Free( log_buf_t *lb );
void LogBuf_Append( log_buf_t *lb, const char *s, size_t n );
void LogBuf_AppendStr( log_buf_t *lb, const char *s );

#endif // XRCON_MCP_LOG_BUF_H
