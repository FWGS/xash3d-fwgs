/*
android.c - android support for filesystem
Copyright (C) 2022 Velaron

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "port.h"

#if XASH_ANDROID

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stddef.h>
#include STDINT_H
#include "filesystem_internal.h"
#include "crtlib.h"
#include "xash3d_mathlib.h"
#include "common/com_strings.h"

#include <jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <time.h>

struct android_assets_s
{
	string package_name;
	qboolean engine;
	AAssetManager *asset_manager;
	AAssetDir *dir;
};

struct jni_methods_s
{
	JNIEnv *env;
	jobject activity;
	jclass activity_class;
	jmethodID getPackageName;
	jmethodID getCallingPackage;
	jmethodID getAssetsList;
	jmethodID getAssets;
} jni;

static void Android_GetAssetManager( android_assets_t *assets )
{
	jobject assetManager;

	assetManager = (*jni.env)->CallObjectMethod( jni.env, jni.activity, jni.getAssets, assets->engine );

	if( assetManager )
		assets->asset_manager = AAssetManager_fromJava( jni.env, assetManager );
	else if( assets->engine )
		Con_Reportf( S_WARN "Couldn't add engine assets!" );
}

static const char *Android_GetPackageName( qboolean engine )
{
	static string pkg;
	jstring resultJNIStr;
	const char *resultCStr;

	resultJNIStr = (*jni.env)->CallObjectMethod( jni.env, jni.activity, engine ? jni.getPackageName : jni.getCallingPackage );

	if( !resultJNIStr )
		return NULL;

	resultCStr = (*jni.env)->GetStringUTFChars( jni.env, resultJNIStr, NULL );
	Q_strncpy( pkg, resultCStr, sizeof( pkg ));
	(*jni.env)->ReleaseStringUTFChars( jni.env, resultJNIStr, resultCStr );

	return pkg;
}

static void Android_ListDirectory( stringlist_t *list, const char *path, qboolean engine )
{
	jstring JStr = (*jni.env)->NewStringUTF( jni.env, path );
	jobjectArray JNIArray = (*jni.env)->CallObjectMethod( jni.env, jni.activity, jni.getAssetsList, engine, JStr );
	int JNIArraySize = (*jni.env)->GetArrayLength( jni.env, JNIArray );

	for( int i = 0; i < JNIArraySize; i++ )
	{
		jstring JNIStr = (*jni.env)->GetObjectArrayElement( jni.env, JNIArray, i );
		const char *CStr = (*jni.env)->GetStringUTFChars( jni.env, JNIStr, NULL );

		stringlistappend( list, (char *)CStr );
		(*jni.env)->ReleaseStringUTFChars( jni.env, JNIStr, CStr );
	}
}

static void FS_CloseAndroidAssets( android_assets_t *assets )
{
	if( assets->dir )
		AAssetDir_close( assets->dir );

	Mem_Free( assets );
}

static android_assets_t *FS_LoadAndroidAssets( qboolean engine )
{
	android_assets_t *assets = Mem_Calloc( fs_mempool, sizeof( *assets ));

	assets->engine = engine;

	Android_GetAssetManager( assets );
	if( !assets->asset_manager )
	{
		Con_Printf( S_ERROR "%s: Can't get asset manager\n", __func__ );
		FS_CloseAndroidAssets( assets );
		return NULL;
	}

	assets->dir = AAssetManager_openDir( assets->asset_manager, "" );
	if( !assets->dir )
	{
		Con_Printf( S_ERROR "%s: Can't open root asset directory\n", __func__ );
		FS_CloseAndroidAssets( assets );
		return NULL;
	}

	return assets;
}

static int FS_FileTime_AndroidAssets( searchpath_t *search, const char *filename )
{
	static time_t time;

	if( !time )
	{
		struct tm file_tm;

		strptime( g_buildcommit_date, "%Y-%m-%d %H:%M:%S", &file_tm );
		time = mktime( &file_tm );
	}

	return time;
}

static int FS_FindFile_AndroidAssets( struct searchpath_s *search, const char *path, char *fixedname, size_t len )
{
	AAsset *assets = AAssetManager_open( search->assets->asset_manager, path, AASSET_MODE_UNKNOWN );

	if( assets )
	{
		AAsset_close( assets );

		Q_strncpy( fixedname, path, len );
		return 0;
	}

	return -1;
}

static void FS_PrintInfo_AndroidAssets( searchpath_t *search, char *dst, size_t size )
{
	Q_snprintf( dst, size, "%s", search->assets->package_name );
}

static void FS_Close_AndroidAssets( searchpath_t *search )
{
	FS_CloseAndroidAssets( search->assets );
}

static void FS_Search_AndroidAssets( searchpath_t *search, stringlist_t *list, const char *pattern, int caseinsensitive )
{
	string temp;
	stringlist_t dirlist;
	const char *slash, *backslash, *colon, *separator;
	int basepathlength, dirlistindex, resultlistindex;
	char *basepath;

	slash = Q_strrchr( pattern, '/' );
	backslash = Q_strrchr( pattern, '\\' );
	colon = Q_strrchr( pattern, ':' );

	separator = Q_max( slash, backslash );
	separator = Q_max( separator, colon );

	basepathlength = separator ? (separator + 1 - pattern) : 0;
	basepath = Mem_Calloc( fs_mempool, basepathlength + 1 );
	if( basepathlength )
		memcpy( basepath, pattern, basepathlength );
	basepath[basepathlength] = '\0';

	stringlistinit( &dirlist );
	Android_ListDirectory( &dirlist, basepath, search->assets->engine );

	Q_strncpy( temp, basepath, sizeof( temp ));

	for( dirlistindex = 0; dirlistindex < dirlist.numstrings; dirlistindex++ )
	{
		Q_strncpy( &temp[basepathlength], dirlist.strings[dirlistindex], sizeof( temp ) - basepathlength );

		if( matchpattern( temp, (char *)pattern, true ))
		{
			for( resultlistindex = 0; resultlistindex < list->numstrings; resultlistindex++ )
			{
				if( !Q_strcmp( list->strings[resultlistindex], temp ))
					break;
			}

			if( resultlistindex == list->numstrings )
				stringlistappend( list, temp );
		}
	}

	stringlistfreecontents( &dirlist );

	Mem_Free( basepath );
}

static file_t *FS_OpenFile_AndroidAssets( searchpath_t *search, const char *filename, const char *mode, int pack_ind )
{
	file_t *file = Mem_Calloc( fs_mempool, sizeof( *file ));
	AAsset *assets = AAssetManager_open( search->assets->asset_manager, filename, AASSET_MODE_RANDOM );

	file->handle = AAsset_openFileDescriptor( assets, &file->offset, &file->real_length );

	file->position = 0;
	file->ungetc = EOF;
	file->searchpath = search;

	AAsset_close( assets );

	return file;
}

static byte *FS_LoadAndroidAssetsFile( searchpath_t *search, const char *path, int pack_ind, fs_offset_t *filesize, void *( *pfnAlloc )( size_t ), void ( *pfnFree )( void * ))
{
	byte *buf;
	off_t size;
	AAsset *asset;

	if( filesize ) *filesize = 0;

	asset = AAssetManager_open( search->assets->asset_manager, path, AASSET_MODE_BUFFER );
	if( !asset )
		return NULL;

	size = AAsset_getLength( asset );

	buf = (byte *)pfnAlloc( size + 1 );
	if( unlikely( !buf ))
	{
		Con_Reportf( "%s: can't alloc %d bytes, no free memory\n", __func__, size + 1 );
		AAsset_close( asset );
		return NULL;
	}

	buf[size] = '\0';

	if( AAsset_read( asset, buf, size ) < 0 )
	{
		pfnFree( buf );
		AAsset_close( asset );
		return NULL;
	}

	AAsset_close( asset );
	if( filesize ) *filesize = size;

	return buf;
}

searchpath_t *FS_AddAndroidAssets_Fullpath( const char *path, int flags )
{
	searchpath_t *search;
	android_assets_t *assets = NULL;
	qboolean engine = true;

	if( !jni.getPackageName || !jni.getCallingPackage || !jni.getAssetsList || !jni.getAssets )
		return NULL;

	if( FBitSet( flags, FS_STATIC_PATH | FS_CUSTOM_PATH ))
		return NULL;

	if( FBitSet( flags, FS_GAMEDIR_PATH ) && Q_stricmp( GI->basedir, GI->gamefolder ))
		engine = false;

	assets = FS_LoadAndroidAssets( engine );

	if( !assets )
	{
		Con_Reportf( S_ERROR "%s: unable to load Android assets \"%s\"\n", __func__, Android_GetPackageName( engine ));
		return NULL;
	}

	Q_strncpy( assets->package_name, Android_GetPackageName( engine ), sizeof( assets->package_name ));

	search = Mem_Calloc( fs_mempool, sizeof( *search ));

	Q_strncpy( search->filename, assets->package_name, sizeof( search->filename ));
	search->assets = assets;
	search->type = SEARCHPATH_ANDROID_ASSETS;
	SetBits( search->flags, FS_NOWRITE_PATH | FS_CUSTOM_PATH );

	search->pfnPrintInfo = FS_PrintInfo_AndroidAssets;
	search->pfnClose = FS_Close_AndroidAssets;
	search->pfnOpenFile = FS_OpenFile_AndroidAssets;
	search->pfnFileTime = FS_FileTime_AndroidAssets;
	search->pfnFindFile = FS_FindFile_AndroidAssets;
	search->pfnSearch = FS_Search_AndroidAssets;
	search->pfnLoadFile = FS_LoadAndroidAssetsFile;

	Con_Reportf( "Adding Android assets: %s\n", assets->package_name );

	return search;
}

void FS_InitAndroid( void )
{
	jmethodID getContext;

	jni.env = (JNIEnv *)Sys_GetNativeObject( "JNIEnv" );
	jni.activity_class = Sys_GetNativeObject( "ActivityClass" );

	if( !jni.env || !jni.activity_class )
	{
		Con_Reportf( S_WARN "%s: unable to get JNI env to load Android assets\n", __func__ );
		return;
	}

	getContext = (*jni.env)->GetStaticMethodID( jni.env, jni.activity_class, "getContext", "()Landroid/content/Context;" );
	jni.activity = (*jni.env)->CallStaticObjectMethod( jni.env, jni.activity_class, getContext );

	jni.getPackageName = (*jni.env)->GetMethodID( jni.env, jni.activity_class, "getPackageName", "()Ljava/lang/String;" );
	jni.getCallingPackage = (*jni.env)->GetMethodID( jni.env, jni.activity_class, "getCallingPackage", "()Ljava/lang/String;" );
	jni.getAssetsList = (*jni.env)->GetMethodID( jni.env, jni.activity_class, "getAssetsList", "(ZLjava/lang/String;)[Ljava/lang/String;" );
	jni.getAssets = (*jni.env)->GetMethodID( jni.env, jni.activity_class, "getAssets", "(Z)Landroid/content/res/AssetManager;" );

	if( !jni.getPackageName || !jni.getCallingPackage || !jni.getAssetsList || !jni.getAssets )
		Con_Reportf( S_WARN "%s: unable to find required JNI interfaces to load Android assets\n", __func__ );
}

#endif // XASH_ANDROID
