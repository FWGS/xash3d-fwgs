/*
probe_save_gc.h - GameCube probe RAM save bank (G94)
*/
#ifndef PROBE_SAVE_GC_H
#define PROBE_SAVE_GC_H

#include "filesystem.h"

#if XASH_GAMECUBE
qboolean GC_ProbeSaveActive( void );
qboolean GC_ProbeSaveFileExists( const char *filename );
file_t *GC_ProbeSaveOpen( const char *filepath, const char *mode );
qboolean GC_ProbeSaveIsHandle( const file_t *file );
fs_offset_t GC_ProbeSaveWrite( file_t *file, const void *data, size_t datasize );
fs_offset_t GC_ProbeSaveRead( file_t *file, void *buffer, size_t buffersize );
void GC_ProbeSaveClose( file_t *file );
fs_offset_t GC_ProbeSaveSeek( file_t *file, fs_offset_t offset, int whence );
void GC_ProbeSaveInitOpens( void );
#else
static inline qboolean GC_ProbeSaveActive( void ) { return false; }
static inline qboolean GC_ProbeSaveFileExists( const char *filename ) { (void)filename; return false; }
static inline file_t *GC_ProbeSaveOpen( const char *filepath, const char *mode )
{
	(void)filepath; (void)mode; return NULL;
}
static inline qboolean GC_ProbeSaveIsHandle( const file_t *file ) { (void)file; return false; }
static inline fs_offset_t GC_ProbeSaveWrite( file_t *file, const void *data, size_t datasize )
{
	(void)file; (void)data; (void)datasize; return 0;
}
static inline fs_offset_t GC_ProbeSaveRead( file_t *file, void *buffer, size_t buffersize )
{
	(void)file; (void)buffer; (void)buffersize; return 0;
}
static inline void GC_ProbeSaveClose( file_t *file ) { (void)file; }
static inline fs_offset_t GC_ProbeSaveSeek( file_t *file, fs_offset_t offset, int whence )
{
	(void)file; (void)offset; (void)whence; return -1;
}
static inline void GC_ProbeSaveInitOpens( void ) { }
#endif

#endif /* PROBE_SAVE_GC_H */
