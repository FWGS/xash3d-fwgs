/*
ViewController.m - do not include this file, only copy the header
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

#import "ViewController.h"
#import "Alert.h"
#import "Game.h"
#import "Globals.h"

@implementation GamesViewController

- (void)viewDidLoad {
	[super viewDidLoad];
	[self getGameDirs];
	[self uiInit];
	[self constraintsInit];
}

- (void)getGameDirs {
	NSError *error;
	NSArray<NSURL*> *dirList = [NSFileManager.defaultManager contentsOfDirectoryAtURL:documentsDirctory includingPropertiesForKeys:@[NSURLIsDirectoryKey] options:NSDirectoryEnumerationSkipsHiddenFiles
		error:&error];
	self.games = [[NSMutableArray alloc] initWithCapacity:dirList.count];
	
	if (error)
	{
		NSLog(@"Failed to enumerate directories: %@", error.localizedDescription);
		return;
	}
	
	for (NSURL *url in dirList)
	{
		NSError *resourceError = nil;
		NSNumber *isdir;
		[url getResourceValue:&isdir forKey:NSURLIsDirectoryKey error:&resourceError];
		
		if (resourceError)
		{
			NSLog(@"Error while checking URL: %@", resourceError.localizedDescription);
			continue;
		}
		
		if(![isdir boolValue])
			continue;
		
		NSString *gameinfoDir = [url.path stringByAppendingPathComponent:@"gameinfo.txt"];
		NSString *liblistDir = [url.path stringByAppendingPathComponent:@"liblist.gam"];
		Game *game;
		
		if ([NSFileManager.defaultManager fileExistsAtPath:gameinfoDir])
		{
			game = [[Game alloc] initWithURL:url gameInfoPath:gameinfoDir];
			[self.games addObject:game];
		}
		else if ([NSFileManager.defaultManager fileExistsAtPath:liblistDir])
		{
			game = [[Game alloc] initWithURL:url gameInfoPath:liblistDir];
			[self.games addObject:game];
		}
		else
			continue;
	}
}

- (void)uiInit {
	UICollectionViewFlowLayout *layoutFlow = [[UICollectionViewFlowLayout alloc] init];
	layoutFlow.scrollDirection = UICollectionViewScrollDirectionVertical;
	layoutFlow.minimumLineSpacing = 20;
	layoutFlow.minimumInteritemSpacing = 0;
	layoutFlow.estimatedItemSize = CGSizeZero;
	
	self.gamesView = [[UICollectionView alloc] initWithFrame:CGRectZero collectionViewLayout:layoutFlow];
	self.gamesView.delegate = self;
	self.gamesView.dataSource = self;
	self.gamesView.backgroundColor = [UIColor clearColor];
	self.gamesView.translatesAutoresizingMaskIntoConstraints = NO;
	self.gamesView.pagingEnabled = NO;
	self.gamesView.showsVerticalScrollIndicator = YES;
	[self.gamesView registerClass:[UICollectionViewCell class] forCellWithReuseIdentifier:@"GameCell"];
	
	[self.view addSubview:self.gamesView];
}

- (void)constraintsInit {
	[NSLayoutConstraint activateConstraints:@[
		[self.gamesView.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor],
			[self.gamesView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
			[self.gamesView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
		[self.gamesView.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
	]];
}

- (nonnull __kindof UICollectionViewCell *)collectionView:(nonnull UICollectionView *)collectionView cellForItemAtIndexPath:(nonnull NSIndexPath *)indexPath {
	UICollectionViewCell *cell = [collectionView dequeueReusableCellWithReuseIdentifier:@"GameCell" forIndexPath:indexPath];
	
	Game *game = [self.games objectAtIndex:indexPath.item];
	UIImageView *view = [game getViewForThumbnail];
	UIToolbar *toolbar = [game getViewForToolbar];
	[cell.contentView addSubview:view];
	[cell.contentView addSubview:toolbar];
	
	[NSLayoutConstraint activateConstraints:@[
		[view.topAnchor constraintEqualToAnchor:cell.topAnchor],
		[view.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor],
		[view.trailingAnchor constraintEqualToAnchor:cell.trailingAnchor],
		[view.bottomAnchor constraintEqualToAnchor:cell.bottomAnchor],
		
		[toolbar.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor],
		[toolbar.trailingAnchor constraintEqualToAnchor:cell.trailingAnchor],
		[toolbar.bottomAnchor constraintEqualToAnchor:cell.bottomAnchor constant:-12],
	]];
	
	return cell;
}

- (NSInteger)collectionView:(nonnull UICollectionView *)collectionView numberOfItemsInSection:(NSInteger)section {
	return self.games.count;
}

- (CGSize)collectionView:(UICollectionView *)collectionView layout:(UICollectionViewLayout*)collectionViewLayout sizeForItemAtIndexPath:(NSIndexPath *)indexPath {
	
	return CGSizeMake(BACKGROUND_WIDTH / 2, BACKGROUND_HEIGHT / 3);
}

@end
