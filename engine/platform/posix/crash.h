/*
crash.h - advanced crashhandler
Copyright (C) 2016 Mittorn

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

//
// crash_libbacktrace.c
//
int Sys_CrashDetailsLibbacktrace( int logfd, char *message, int len, size_t max_len );
qboolean Sys_SetupLibbacktrace( const char *argv0 );

//
// crash_glibc.c
//
int Sys_CrashDetailsExecinfo( int logfd, char *message, int len, size_t max_len );
