/*
Downloader.h - do not include this file, only copy the header
Copyright (C) 2015-2025 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#import <Foundation/Foundation.h>
#import "Game.h"

@interface LibDownloader : NSObject

typedef struct {
	NSDictionary *modKey;
	NSString *filename;
	NSString *sha256;
	NSDictionary *sourceJson;
} t_gameentry;


@property NSDictionary *manifest;

- (BOOL)setEntry:(t_gameentry*)entry forGame:(Game *)game;
- (BOOL)fetchLibsForGame:(Game *)game;

@end

extern NSString *const RELEASE_BASE_URL;
extern NSString *const MANIFEST_URL;
extern NSUInteger const MANIFEST_VERSION;
