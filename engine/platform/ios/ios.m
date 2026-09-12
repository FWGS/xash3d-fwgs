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

#include "SDL_syswm.h"
#import <AdSupport/AdSupport.h>
#import <AppTrackingTransparency/AppTrackingTransparency.h>
#import <UIKit/UIKit.h>
#import <SDL2/SDL.h>
#import <Security/Security.h>

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

	if (@available( iOS 14, *))
	{
		if (ATTrackingManager.trackingAuthorizationStatus == ATTrackingManagerAuthorizationStatusAuthorized)
		{
			NSString *id = ASIdentifierManager.sharedManager.advertisingIdentifier.UUIDString;
			strncpy( udid, id.UTF8String, sizeof(udid) - 1);
			return udid;
		}
	}
	else 
	{
		if (ASIdentifierManager.sharedManager.isAdvertisingTrackingEnabled)
		{
			NSString *id = ASIdentifierManager.sharedManager.advertisingIdentifier.UUIDString;
			strncpy( udid, id.UTF8String, sizeof(udid) - 1);
			return udid;
		}
	}


	NSDictionary *getattrs = @{
		(__bridge NSString *)kSecClass : (__bridge NSString *)kSecClassGenericPassword,
		(__bridge NSString *)kSecAttrAccount: @"XashUUID",
        (__bridge NSString *)kSecReturnData: @YES,
	};

	CFTypeRef result;
	if (SecItemCopyMatching((__bridge CFDictionaryRef)getattrs, &result) == errSecSuccess)
	{
		NSData *data = (__bridge NSData *)result;
		NSString *id = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
		strncpy( udid, [id UTF8String], sizeof(udid) - 1 );
		CFRelease(result);
		NSLog(@"UDID: %s", udid);
		data = nil;
		return udid;
	}

	NSString *id = NSUUID.UUID.UUIDString;
	NSData *idBytes = [id dataUsingEncoding:NSUTF8StringEncoding];

	NSDictionary *setattrs = @{
		(__bridge NSString *)kSecClass : (__bridge NSString *)kSecClassGenericPassword,
		(__bridge NSString *)kSecAttrAccount: @"XashUUID",
        (__bridge NSString *)kSecValueData: idBytes,
	};
	
	OSStatus retval = SecItemAdd((__bridge CFDictionaryRef)setattrs, nil);
	if (retval != errSecSuccess)
	{
		NSLog(@"Xash: Failed to set UDID: SecItemAdd returned %d", retval);
		return nil;
	}
	
	strncpy( udid, id.UTF8String, sizeof(udid) - 1);
	
	return udid;
}

void IOS_Log( const char *text )
{
	NSLog(@"Xash: %@", [NSString stringWithUTF8String:text]);
}
