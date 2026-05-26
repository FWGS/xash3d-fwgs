#include "filesystem_test_common.h"

static int TestNoInit( void )
{
	char nice[69];
	fs_dllinfo_t dllinfo;
	void *p;

	// here are the APIs that shouldn't fail without calling InitStdio
	g_fs.ShutdownStdio();
	g_fs.ClearSearchPath();
	g_fs.AllowDirectPaths( true );
	g_fs.AllowDirectPaths( false );
	if( g_fs.Search( "asfjkajk", 0, 0 ) != 0 )
		return 0;
	g_fs.SetCurrentDirectory( "." ); // must succeed!
	g_fs.FindLibrary( "kek", true, &dllinfo );
	if( g_fs.Open( "afbvasvwerf", "w+", true ) != 0 )
		return 0;
	if( g_fs.LoadFile( "hehe", NULL, true ) != 0 )
		return 0;
	p = g_fs.LoadDirectFile( "hehe", NULL );
	if( p ) free( p );

	if( !g_fs.IsArchiveExtensionSupported( "pk3", 0 )) return 0;
	if( !g_fs.IsArchiveExtensionSupported( "wad", 0 )) return 0;
	if( !g_fs.IsArchiveExtensionSupported( "pak", 0 )) return 0;
	if( !g_fs.IsArchiveExtensionSupported( "pk3dir", 0 )) return 0;

	if( !g_fs.IsArchiveExtensionSupported( "pk3", IAES_ONLY_REAL_ARCHIVES )) return 0;
	if( g_fs.IsArchiveExtensionSupported( "pk3dir", IAES_ONLY_REAL_ARCHIVES )) return 0;
	if( g_fs.IsArchiveExtensionSupported( "vpk", 0)) return 0;

	g_fs.FileExists( "asdcv", 0 );
	g_fs.FileTime( "zxcasdfd", 0 );
	g_fs.FileSize( "asdqwe", 1 );
	g_fs.Rename( "cafqefv", "zvewfw" );
	g_fs.SysFileExists( "zbarbgaer" );
	g_fs.GetDiskPath( "aggasdfbaergba", 0 );
	g_fs.GetFullDiskPath( nice, sizeof( nice ), "oh my!", true );
	g_fs.Delete( "blabla" );

	return 1;
}

int main( void )
{
	if( !LoadFilesystem() )
		return EXIT_FAILURE;

	if( !TestNoInit( ))
		return EXIT_FAILURE;

	FreeLibrary( g_hModule );

	printf( "success\n" );

	return EXIT_SUCCESS;
}
