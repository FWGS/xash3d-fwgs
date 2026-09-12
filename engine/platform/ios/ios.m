/*
 launchdialog.m - iOS lauch dialog
 Copyright (C) 2016 mittorn
 
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 */

#include <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <SDL2/SDL.h>

#define XASHLIB "@rpath/libxash.dylib"

const char *IOS_GetDocsDir( void )
{
	static const char *dir = NULL;
	
	if( dir )
		return dir;
	
	NSString *documentsDirctory = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES)[0];
	[NSFileManager.defaultManager createDirectoryAtPath:documentsDirctory withIntermediateDirectories:YES attributes:nil error:nil];
	
	dir = documentsDirctory.fileSystemRepresentation;
	NSLog(@"IOS_GetDocsDir: %s", dir);
	
	return dir;
}

const char *IOS_GetExecDir( void )
{
	static const char *dir = NULL;
	
	if( dir )
		return dir;
	
	NSString *executableDirctory = [[NSBundle mainBundle] bundlePath];
	
	dir = executableDirctory.fileSystemRepresentation;
	NSLog(@"IOS_GetExecDir: %s", dir);
	
	return dir;
}

void IOS_PrepareView( void )
{
	SDL_SetMainReady();
	SDL_iPhoneSetEventPump(SDL_TRUE);
}

char *IOS_GetUDID( void )
{
	static char udid[256];
	NSString *id = UIDevice.currentDevice.identifierForVendor.UUIDString;
	strncpy( udid, [id UTF8String], 255 );
	[id release];

	return udid;
}

void IOS_Log( const char *text )
{
	NSLog(@"Xash: %@", [NSString stringWithUTF8String:text]);
}
