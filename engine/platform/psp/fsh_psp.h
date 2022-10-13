/*
fsh_psp.h - PSP filesystem helper header
Copyright (C) 2022 Sergey Galushko

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef FSH_PSP_H
#define FSH_PSP_H

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef struct fsh_handle_s	fsh_handle_t;

int FSH_AddFilePath( fsh_handle_t *handle, char *path );
int FSH_RemoveFilePath( fsh_handle_t *handle, char *path );
int FSH_Find( fsh_handle_t *handle, char *path );
fsh_handle_t *FSH_Init( char *path, int maxfiles );
void FSH_Shutdown( fsh_handle_t *handle );

#ifdef __cplusplus
}
#endif

#endif // P5RAM_PSP_H
