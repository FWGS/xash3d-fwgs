/*
crtlib.h - internal stdlib
Copyright (C) 2011 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef STDLIB_H
#define STDLIB_H

// timestamp modes
enum
{
	TIME_FULL = 0,
	TIME_DATE_ONLY,
	TIME_TIME_ONLY,
	TIME_NO_SECONDS,
	TIME_YEAR_ONLY,
	TIME_FILENAME,
};

#define CMD_SERVERDLL	BIT( 0 )		// added by server.dll
#define CMD_CLIENTDLL	BIT( 1 )		// added by client.dll
#define CMD_GAMEUIDLL	BIT( 2 )		// added by GameUI.dll

typedef void (*xcommand_t)( void );

//
// cmd.c
//
void Cbuf_Init( void );
void Cbuf_Clear( void );
void Cbuf_AddText( const char *text );
void Cbuf_InsertText( const char *text );
void Cbuf_ExecStuffCmds( void );
void Cbuf_Execute (void);
uint Cmd_Argc( void );
char *Cmd_Args( void );
char *Cmd_Argv( int arg );
void Cmd_Init( void );
void Cmd_Unlink( int group );
void Cmd_AddCommand( const char *cmd_name, xcommand_t function, const char *cmd_desc );
void Cmd_AddServerCommand( const char *cmd_name, xcommand_t function );
int Cmd_AddClientCommand( const char *cmd_name, xcommand_t function );
int Cmd_AddGameUICommand( const char *cmd_name, xcommand_t function );
void Cmd_RemoveCommand( const char *cmd_name );
qboolean Cmd_Exists( const char *cmd_name );
void Cmd_LookupCmds( char *buffer, void *ptr, setpair_t callback );
qboolean Cmd_GetMapList( const char *s, char *completedname, int length );
qboolean Cmd_GetDemoList( const char *s, char *completedname, int length );
qboolean Cmd_GetMovieList( const char *s, char *completedname, int length );
void Cmd_TokenizeString( char *text );
void Cmd_ExecuteString( char *text );
void Cmd_ForwardToServer( void );

//
// crtlib.c
//
#define Q_strupr( in, out ) Q_strnupr( in, out, 99999 )
void Q_strnupr( const char *in, char *out, size_t size_out );
#define Q_strlwr( in, out ) Q_strnlwr( in, out, 99999 )
void Q_strnlwr( const char *in, char *out, size_t size_out );
int Q_strlen( const char *string );
int Q_colorstr( const char *string );
char Q_toupper( const char in );
char Q_tolower( const char in );
#define Q_strcat( dst, src ) Q_strncat( dst, src, 99999 )
size_t Q_strncat( char *dst, const char *src, size_t siz );
#define Q_strcpy( dst, src ) Q_strncpy( dst, src, 99999 )
size_t Q_strncpy( char *dst, const char *src, size_t siz );
#define copystring( s ) _copystring( host.mempool, s, __FILE__, __LINE__ )
#define SV_CopyString( s ) _copystring( svgame.stringspool, s, __FILE__, __LINE__ )
#define freestring( s ) if( s != NULL ) { Mem_Free( s ); s = NULL; }
char *_copystring( byte *mempool, const char *s, const char *filename, int fileline );
uint Q_hashkey( const char *string, uint hashSize, qboolean caseinsensitive );
qboolean Q_isdigit( const char *str );
int Q_atoi( const char *str );
float Q_atof( const char *str );
void Q_atov( float *vec, const char *str, size_t siz );
char *Q_strchr( const char *s, char c );
char *Q_strrchr( const char *s, char c );
#define Q_stricmp( s1, s2 ) Q_strnicmp( s1, s2, 99999 )
int Q_strnicmp( const char *s1, const char *s2, int n );
#define Q_strcmp( s1, s2 ) Q_strncmp( s1, s2, 99999 )
int Q_strncmp( const char *s1, const char *s2, int n );
qboolean Q_stricmpext( const char *s1, const char *s2 );
const char *Q_timestamp( int format );
char *Q_stristr( const char *string, const char *string2 );
char *Q_strstr( const char *string, const char *string2 );
#define Q_vsprintf( buffer, format, args ) Q_vsnprintf( buffer, 99999, format, args )
int Q_vsnprintf( char *buffer, size_t buffersize, const char *format, va_list args );
int Q_snprintf( char *buffer, size_t buffersize, const char *format, ... );
int Q_sprintf( char *buffer, const char *format, ... );
#define Q_memprint( val ) Q_pretifymem( val, 2 )
char *Q_pretifymem( float value, int digitsafterdecimal );
char *va( const char *format, ... );

//
// zone.c
//
void Memory_Init( void );
void *_Mem_Realloc( byte *poolptr, void *memptr, size_t size, qboolean clear, const char *filename, int fileline );
void *_Mem_Alloc( byte *poolptr, size_t size, qboolean clear, const char *filename, int fileline );
byte *_Mem_AllocPool( const char *name, const char *filename, int fileline );
void _Mem_FreePool( byte **poolptr, const char *filename, int fileline );
void _Mem_EmptyPool( byte *poolptr, const char *filename, int fileline );
void _Mem_Free( void *data, const char *filename, int fileline );
void _Mem_Check( const char *filename, int fileline );
qboolean Mem_IsAllocatedExt( byte *poolptr, void *data );
void Mem_PrintList( size_t minallocationsize );
void Mem_PrintStats( void );

#define Mem_Malloc( pool, size ) _Mem_Alloc( pool, size, false, __FILE__, __LINE__ )
#define Mem_Calloc( pool, size ) _Mem_Alloc( pool, size, true, __FILE__, __LINE__ )
#define Mem_Realloc( pool, ptr, size ) _Mem_Realloc( pool, ptr, size, true, __FILE__, __LINE__ )
#define Mem_Free( mem ) _Mem_Free( mem, __FILE__, __LINE__ )
#define Mem_AllocPool( name ) _Mem_AllocPool( name, __FILE__, __LINE__ )
#define Mem_FreePool( pool ) _Mem_FreePool( pool, __FILE__, __LINE__ )
#define Mem_EmptyPool( pool ) _Mem_EmptyPool( pool, __FILE__, __LINE__ )
#define Mem_IsAllocated( mem ) Mem_IsAllocatedExt( NULL, mem )
#define Mem_Check() _Mem_Check( __FILE__, __LINE__ )
	
#endif//STDLIB_H