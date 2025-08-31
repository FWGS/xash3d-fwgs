/*
fscallback.h - common filesystem callbacks
Copyright (C) 2003-2006 Mathieu Olivier
Copyright (C) 2000-2007 DarkPlaces contributors
Copyright (C) 2007 Uncle Mike
Copyright (C) 2015-2023 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef FSCALLBACK_H
#define FSCALLBACK_H

#include "filesystem.h"

extern fs_api_t g_fsapi;
extern fs_globals_t *FI;

#define GI           FI->GameInfo
#define FS_Gamedir() GI->gamefolder
#define FS_Title()   GI->title

#define FS_InitStdio     (*g_fsapi.InitStdio)
#define FS_ShutdownStdio (*g_fsapi.ShutdownStdio)

// search path utils
#define FS_Rescan (*g_fsapi.Rescan)
#define FS_ClearSearchPath (*g_fsapi.ClearSearchPath)
#define FS_AllowDirectPaths (*g_fsapi.AllowDirectPaths)
#define FS_AddGameDirectory (*g_fsapi.AddGameDirectory)
#define FS_AddGameHierarchy (*g_fsapi.AddGameHierarchy)
#ifndef FSCALLBACK_OVERRIDE_MALLOC_LIKE
#define FS_Search (*g_fsapi.Search)
#endif
#define FS_SetCurrentDirectory (*g_fsapi.SetCurrentDirectory)
#define FS_Path_f (*g_fsapi.Path_f)

// file ops
#ifndef FSCALLBACK_OVERRIDE_MALLOC_LIKE
#define FS_Open (*g_fsapi.Open)
#define FS_Close (*g_fsapi.Close)
#endif
#define FS_Write (*g_fsapi.Write)
#define FS_Read (*g_fsapi.Read)
#define FS_Seek (*g_fsapi.Seek)
#define FS_Tell (*g_fsapi.Tell)
#define FS_Eof (*g_fsapi.Eof)
#define FS_Flush (*g_fsapi.Flush)
#define FS_Gets (*g_fsapi.Gets)
#define FS_UnGetc (*g_fsapi.UnGetc)
#define FS_Getc (*g_fsapi.Getc)
#define FS_VPrintf (*g_fsapi.VPrintf)
#define FS_Printf (*g_fsapi.Printf)
#define FS_Print (*g_fsapi.Print)
#define FS_FileLength (*g_fsapi.FileLength)
#define FS_FileCopy (*g_fsapi.FileCopy)

// file buffer ops
#ifndef FSCALLBACK_OVERRIDE_MALLOC_LIKE
#define FS_LoadFile (*g_fsapi.LoadFile)
#define FS_LoadDirectFile (*g_fsapi.LoadDirectFile)
#endif
#define FS_WriteFile (*g_fsapi.WriteFile)

// file hashing
#define CRC32_File (*g_fsapi.CRC32_File)
#define MD5_HashFile (*g_fsapi.MD5_HashFile)

// filesystem ops
#define FS_FileExists (*g_fsapi.FileExists)
#define FS_FileTime (*g_fsapi.FileTime)
#define FS_FileSize (*g_fsapi.FileSize)
#define FS_Rename (*g_fsapi.Rename)
#define FS_Delete (*g_fsapi.Delete)
#define FS_SysFileExists (*g_fsapi.SysFileExists)
#define FS_GetDiskPath (*g_fsapi.GetDiskPath)


#endif // FSCALLBACK_H
