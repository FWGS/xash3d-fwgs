/*
Game.h - do not include this file, only copy the header
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
#import <UIKit/UIKit.h>

typedef struct {
	BOOL hd_background;
	NSString *gamedll_osx;
	NSString *gamedll_linux;
	NSString *gamedll;
	NSString *dll_path;
	NSString *title;
} t_gameinfo;

/*typedef enum : NSUInteger {
	LOADABLE,
	NEEDSIGN,
	MISCERROR,
} t_gamestatus;*/

@interface Game : NSObject

@property(nonatomic) NSURL *URL;
@property(nonatomic) NSString *const gameDir;
@property(nonatomic) NSString *const gameinfoPath;
@property(nonatomic) t_gameinfo *gameInfo;
@property(nonatomic) NSArray<NSString*> *libList;
@property(nonatomic) UIImageView *thumbnailView;
@property(nonatomic) UIToolbar *thumbnailToolbar;
@property(nonatomic) UIImage *thumbnail;

- (instancetype)initWithURL:(NSURL*)url gameInfoPath:(NSString*)infoPat;

// Check if the game libs are signed
- (BOOL)isLoadable;

- (UIImageView*)getViewForThumbnail;
- (UIToolbar*)getViewForToolbar;

- (IBAction)startGame;
- (BOOL)signGame;

@end

#define BACKGROUND_ROWS 3
#define BACKGROUND_COLUMNS 4
#define BACKGROUND_WIDTH 800
#define BACKGROUND_HEIGHT 600
#define DEFAULT_GAMEDIR "valve"
