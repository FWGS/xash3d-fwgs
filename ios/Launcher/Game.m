/*
Game.m - do not include this file, only copy the header
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

//too many includes?
#import "AppDelegate.h"
#import "Game.h"
#import "GameSettings.h"
#import "Signing.h"
#import "dlfcn.h"
#import "Downloader.h"
#import "Globals.h"
#import "Alert.h"

Signer *signer;
LibDownloader *downloader;

@interface Game ()

@property(nonatomic) GameSettings *settingsView;

@end

@implementation Game

- (instancetype)initWithURL:(NSURL*)url gameInfoPath:(NSString*)infoPath{
	self = [super init];
	self.URL = url;
	self.gameDir = url.lastPathComponent;
	self.gameinfoPath = infoPath;
	self.gameInfo = malloc(sizeof(t_gameinfo));
	memset((void*)self.gameInfo, 0, sizeof(t_gameinfo));
	[self parsegameInfo];
	[self initThumbnail];
	if (!downloader)
		downloader = [LibDownloader new];
	
	return self;
}

- (void)initThumbnail {
	NSString *layout;
	NSString *bmpPath;
	
	if (self.gameInfo->hd_background)
	{
		layout = [self.URL.path stringByAppendingPathComponent:@"resource/HD_BackgroundLayout.txt"];
		if (![NSFileManager.defaultManager fileExistsAtPath:layout])
			layout = nil;
	}
	
	if (!layout)
	{
		layout = [self.URL.path stringByAppendingPathComponent:@"resource/BackgroundLayout.txt"];
	}
	
	if (![NSFileManager.defaultManager fileExistsAtPath:layout])
	{
	loadwithoutlayout:
		bmpPath = [self.URL.path stringByAppendingPathComponent:@"gfx/shell/splash.bmp"];
		
		if ([NSFileManager.defaultManager fileExistsAtPath:bmpPath])
			self.thumbnail = [UIImage imageWithContentsOfFile:bmpPath];
		else
			self.thumbnail = [self drawWithoutLayout];
	}
	else
	{
		NSString *layoutData;
		NSError *error;
		layoutData = [NSString stringWithContentsOfFile:layout encoding:NSUTF8StringEncoding error:&error];
		if (error)
		{
			NSLog(@"Failed to load background layout: %@", error.localizedDescription);
			goto loadwithoutlayout;
		}
		self.thumbnail = [self drawLayoutWithData:layoutData];
	}
}

- (UIImage*)drawWithoutLayout {
	UIGraphicsImageRenderer *renderer = [[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(BACKGROUND_WIDTH, BACKGROUND_HEIGHT)];
	UIImage *fullImage = [renderer imageWithActions:^(UIGraphicsImageRendererContext * _Nonnull rendererContext) {
		int x,y = 0;
		CGRect drawRect;
		for (int i = 0; i < BACKGROUND_ROWS; i++)
		{
			x = 0;
			int height = 0;
			for (int j = 0; j < BACKGROUND_COLUMNS; j++)
			{
				NSString *path = [NSString stringWithFormat:@"%@/%d_%d_%c_loading.tga", [self.URL.path stringByAppendingPathComponent:@"resource/background"], BACKGROUND_WIDTH, i + 1, 'a' + j];
				NSData *tileData = [[NSData alloc] initWithContentsOfFile:path];
				UIImage *tile = [UIImage imageWithData:tileData];
				
				drawRect = CGRectMake(x, y, tile.size.width, tile.size.height);
				[tile drawInRect:drawRect];
				
				x += tile.size.width;
				height = tile.size.height;
			}
			y += height;
		}
	}];
	
	return fullImage;
}

- (UIImage*)drawLayoutWithData:(NSString*)layout {
	NSScanner *scanner = [NSScanner scannerWithString:layout];
	NSInteger width, height = 0;
	
	NSString *resStr;
	[scanner scanUpToCharactersFromSet:[NSCharacterSet whitespaceAndNewlineCharacterSet] intoString:&resStr];
	if ([resStr isEqualToString:@"resolution"])
	{
		[scanner scanInteger:&width];
		[scanner scanInteger:&height];
	}
	else
	{
		NSLog(@"Layout data is not valid!");
		return nil;
	}
	
	UIGraphicsImageRenderer *renderer = [[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(width, height)];
	UIImage *fullImage = [renderer imageWithActions:^(UIGraphicsImageRendererContext * _Nonnull rendererContext) {
		while (![scanner isAtEnd])
		{
			NSString *tilePath;
			[scanner scanUpToCharactersFromSet:[NSCharacterSet whitespaceAndNewlineCharacterSet] intoString:&tilePath];
			
			NSData *tileData = [[NSData alloc] initWithContentsOfURL:[NSURL fileURLWithPath:tilePath isDirectory:NO relativeToURL:self.URL]];
			UIImage *tile = [UIImage imageWithData:tileData];
			
			//discard
			[scanner scanUpToCharactersFromSet:[NSCharacterSet whitespaceAndNewlineCharacterSet] intoString:nil];
			
			float x;
			float y;
			
			[scanner scanFloat:&x];
			[scanner scanFloat:&y];
			
			CGRect drawRect = CGRectMake(x, y, tile.size.width, tile.size.height);
			[tile drawInRect:drawRect];
		}
	}];
	
	return fullImage;
}

//get needed info from gameinfo.txt/liblist.gam.
- (void)parsegameInfo {
	NSError *error;
	NSString *infoStr = [[NSString alloc] initWithContentsOfFile:self.gameinfoPath encoding:NSASCIIStringEncoding error:&error];
	
	if (error)
	{
		[Alert displayErrorAlertWithMessage:[NSString stringWithFormat:@"Error while opening info file for %@: %@", self.gameDir, error.localizedDescription]];
		return;
	}
	
	if (infoStr.length == 0)
	{
		NSLog(@"Game info file is empty!");
		return;
	}
	
	NSCharacterSet *quotesSet = [NSCharacterSet characterSetWithCharactersInString:@"\""];
	NSScanner *scanner = [NSScanner scannerWithString:infoStr];
	while(!scanner.isAtEnd)
	{
		NSString *token;
		[scanner scanUpToCharactersFromSet:[NSCharacterSet whitespaceAndNewlineCharacterSet] intoString:&token];
		
		if ([token isEqualToString:@"hd_background"])
		{
			int value;
			[scanner scanInt:&value];
			self.gameInfo->hd_background = value;
			continue;
		}
		else if ([token isEqualToString:@"gamedll_osx"])
		{
			NSString *value;
			[scanner scanUpToCharactersFromSet:[NSCharacterSet whitespaceAndNewlineCharacterSet] intoString:&value];
			self.gameInfo->gamedll_osx = [value stringByTrimmingCharactersInSet:quotesSet];
			continue;
		}
		else if ([token isEqualToString:@"gamedll_linux"])
		{
			NSString *value;
			[scanner scanUpToCharactersFromSet:[NSCharacterSet whitespaceAndNewlineCharacterSet] intoString:&value];
			self.gameInfo->gamedll_linux = [value stringByTrimmingCharactersInSet:quotesSet];
			continue;
		}
		else if ([token isEqualToString:@"gamedll"])
		{
			NSString *value;
			[scanner scanUpToCharactersFromSet:[NSCharacterSet whitespaceAndNewlineCharacterSet] intoString:&value];
			self.gameInfo->gamedll = [value stringByTrimmingCharactersInSet:quotesSet];
			continue;
		}
		else if ([token isEqualToString:@"dll_path"])
		{
			NSString *value;
			[scanner scanUpToCharactersFromSet:[NSCharacterSet whitespaceAndNewlineCharacterSet] intoString:&value];
			self.gameInfo->dll_path = [value stringByTrimmingCharactersInSet:quotesSet];
			continue;
		}
		else if ([token isEqualToString:@"title"] || [token isEqualToString:@"game"])
		{
			NSString *value;
			[scanner scanUpToCharactersFromSet:[NSCharacterSet whitespaceAndNewlineCharacterSet] intoString:&value];
			self.gameInfo->title = [value stringByTrimmingCharactersInSet:quotesSet];
			continue;
		}
	}
	
	[self loggameInfo];
}

- (void)loggameInfo {
	NSLog(@"title: %@\nhd_background: %d\ngamedll_osx: %@\ngamedll_linux: %@\ngamedll: %@\ndll_path: %@", self.gameInfo->title,  self.gameInfo->hd_background, self.gameInfo->gamedll_osx, self.gameInfo->gamedll_linux, self.gameInfo->gamedll, self.gameInfo->dll_path);
}

- (BOOL)isLoadable {
	BOOL retVal = YES;
	if (!self.libList)
		[self initLibList];
	
	for (NSString *lib in self.libList)
	{
		const char *cPath;
		if (![lib isAbsolutePath])
			cPath = [self.URL.path stringByAppendingPathComponent:lib].UTF8String;
		else
			cPath = lib.UTF8String;
		
		void *tempHandle = dlopen(cPath, RTLD_LAZY);
		if (!tempHandle)
		{
			const char *error = dlerror();
			retVal = NO;
			
			NSLog(@"Library %@ not loadable due to error: %s", lib, error);
		}
	}
	
	return retVal;
}

-(IBAction)startGame {
	//has to be called this way to avoid ui problems in engine (mainly when displaying SDL_ShowMessageBoxSimple)
	[(AppDelegate*)UIApplication.sharedApplication.delegate performSelector:@selector(runEngine:)
				   withObject:self
				   afterDelay:0.0];
}

-(void)initUnsupportedLibList {
	NSString *gamedllname;
	NSString *dll_path;
	
	if (self.gameInfo->gamedll_osx)
		gamedllname = self.gameInfo->gamedll_osx.stringByDeletingPathExtension;
	else if (self.gameInfo->gamedll_linux)
		gamedllname = self.gameInfo->gamedll_linux.stringByDeletingPathExtension;
	else if (self.gameInfo->gamedll)
		gamedllname = self.gameInfo->gamedll.stringByDeletingPathExtension;
	
	if (self.gameInfo->dll_path)
		dll_path = [self.URL.path stringByAppendingPathComponent:self.gameInfo->dll_path];
	else
		dll_path = [self.URL.path stringByAppendingPathComponent:@"cl_dlls"];
	
	NSMutableArray<NSString*> *list = [[NSMutableArray alloc] init];
	[list addObject:[gamedllname stringByAppendingString:@"_ios_arm64.dylib"]];
	NSError *error;
	for (NSString *file in [NSFileManager.defaultManager contentsOfDirectoryAtPath:dll_path error:&error])
	{
		if (error)
		{
			NSLog(@"Error while listing dlls: %@", error.localizedDescription);
			break;
		}
		
		//check if the library is for ios
		NSUInteger searchIndex;
		if (file.lastPathComponent.length > @"_ios_arm64.dylib".length)
		{
			searchIndex = file.lastPathComponent.length - @"_ios_arm64.dylib".length;
		}
		else
			continue;
		
		if (![[file.lastPathComponent substringFromIndex:searchIndex] isEqualToString:@"_ios_arm64.dylib"])
		{
			continue;
		}
		
		[list addObject:[dll_path stringByAppendingPathComponent:file]];
	}
	
	self.libList = list.copy;
}

-(void)initLibList {
	NSURL *libsURL = [NSURL fileURLWithPath:[libraryDirectory stringByAppendingFormat:@"/%@-ios-arm64", self.gameDir]];
	
	if (![NSFileManager.defaultManager fileExistsAtPath:libsURL.path])
	{
		if (![downloader fetchLibsForGame:self])
		{
			//try to guess with other logic
			return [self initUnsupportedLibList];
		}
	}
	
	NSURL *searchURL = [NSURL fileURLWithPath:self.gameDir relativeToURL:libsURL];
	//ideally the paths we get should be relative to game dir
	NSDirectoryEnumerator *enumerator = [NSFileManager.defaultManager enumeratorAtURL:searchURL includingPropertiesForKeys:@[NSURLIsDirectoryKey] options:NSDirectoryEnumerationSkipsHiddenFiles | NSDirectoryEnumerationProducesRelativePathURLs errorHandler:^BOOL(NSURL * _Nonnull url, NSError * _Nonnull error) {
		[Alert displayErrorAlertWithMessage:[NSString stringWithFormat:@"Failed to enumerate directory with error: %@", error.localizedDescription]]
		
		return YES;
	}];
	NSMutableArray *list = [[NSMutableArray alloc] init];
		

	for (NSURL *url in enumerator)
	{
		NSNumber *isDir;
		NSError *error;
		[url getResourceValue:&isDir forKey:NSURLIsDirectoryKey error:&error];
		
		if ([isDir boolValue])
		{
			continue;
		}
		
		if (![url.pathExtension isEqualToString:@"dylib"])
		{
			continue;
		}
		
		NSString *relativePath = [url.relativePath stringByStandardizingPath];
		[list addObject:relativePath];
		
		if ([NSFileManager.defaultManager fileExistsAtPath:[self.URL.path stringByAppendingPathComponent:relativePath]])
			[NSFileManager.defaultManager removeItemAtPath:[self.URL.path stringByAppendingPathComponent:relativePath] error:nil];
		
		
		NSError *copyerror;
		[NSFileManager.defaultManager copyItemAtURL:url.absoluteURL toURL:[NSURL URLWithString:relativePath relativeToURL:self.URL] error:&copyerror];
		if (copyerror)
			NSLog(@"Failed to copy lib due to error: %@", copyerror.localizedDescription);
	}
	
	NSError *deleteerror;
	[NSFileManager.defaultManager removeItemAtURL:libsURL error:&deleteerror];
	
	if (deleteerror)
		NSLog(@"Failed to delete file at path %@ due to error: %@", libsURL.path, deleteerror.localizedDescription);
	
	if (list.count > 0)
		self.libList = list.copy;
	
}

- (void)dealloc {
	free(self.gameInfo);
	self.gameInfo = nil;
}

- (BOOL)signGame {
	if (!signer)
	{
		NSString *certPath = [NSUserDefaults.standardUserDefaults objectForKey:@"P12 PATH"];
		if (!certPath)
		{
			[Alert displayErrorAlertWithMessage:@"Select a P12 certificate before running a game!"];
			return NO;
		}
		
		NSURL *certURL = [NSURL fileURLWithPath:certPath isDirectory:NO relativeToURL:documentsDirctory];
		NSURL *provisionURL;
		NSString *provisionPath = [NSUserDefaults.standardUserDefaults objectForKey:@"PROVISION PATH"];
		
		if (provisionPath)
		{
			provisionURL = [NSURL fileURLWithPath:provisionPath relativeToURL:documentsDirctory];
		}
		
		NSString *password = [NSUserDefaults.standardUserDefaults objectForKey:@"P12 PASSWORD"];
		
		if (!certURL)
		{
			[Alert displayErrorAlertWithMessage:@"Please select a certificate before trying to run a game"];
			return NO;
		}
		
		if (!provisionURL)
			NSLog(@"User did not specify a provision file");
		
		signer = [[Signer alloc] initWithCert:certURL withProvision:provisionURL password:password];
		
		if (!signer)
		{
			NSLog(@"Failed to create signer!");
			return NO;
		}
	}
	
	for (NSString *lib in self.libList)
	{
		NSURL *libURL;
		if (![lib isAbsolutePath])
			libURL = [[NSURL alloc] initWithString:lib relativeToURL:self.URL];
		else
			libURL = [[NSURL alloc] initFileURLWithPath:lib];
		
		if (![signer signAtURL:libURL])
			return NO;
		
	}
	
	return YES;
}

- (UIImageView*)getViewForThumbnail {
	if (self.thumbnailView)
		return self.thumbnailView;
	
	self.thumbnailView = [[UIImageView alloc] initWithImage:self.thumbnail];
	self.thumbnailView.layer.cornerRadius = 20;
	self.thumbnailView.layer.masksToBounds = YES;
	self.thumbnailView.translatesAutoresizingMaskIntoConstraints = NO;
	self.thumbnailView.contentMode = UIViewContentModeScaleToFill;
	
	return self.thumbnailView;
}

- (UIToolbar*)getViewForToolbar {
	//bar items
	UIBarButtonItem *startbutton = [[UIBarButtonItem alloc] initWithImage:[UIImage systemImageNamed:@"play.fill"] style:UIBarButtonItemStylePlain target:self action:@selector(startGame)];
	UIBarButtonItem *settingsbutton = [[UIBarButtonItem alloc] initWithImage:[UIImage systemImageNamed:@"gear"] style:UIBarButtonItemStylePlain target:self action:@selector(displaySettings)];
	UIBarButtonItem *title = [[UIBarButtonItem alloc] initWithTitle:self.gameInfo->title style:UIBarButtonItemStylePlain target:nil action:nil];
	title.customView.userInteractionEnabled = NO;
	
	self.settingsView = [[GameSettings alloc] initWithGame:self];
	self.thumbnailToolbar = [[UIToolbar alloc] init];
	self.thumbnailToolbar.translatesAutoresizingMaskIntoConstraints = NO;
	[self.thumbnailToolbar setItems:@[startbutton, settingsbutton, title] animated:YES];
	
	return self.thumbnailToolbar;
}

-(void)displaySettings {
	AppDelegate * __weak delegate = (AppDelegate *)UIApplication.sharedApplication.delegate;
	[delegate.gamesNavController pushViewController:self.settingsView animated:YES];
}

@end
