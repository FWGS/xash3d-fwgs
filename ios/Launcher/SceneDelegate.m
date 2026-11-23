/*
SceneDelegate.m - Delegate for managing app window lifecycle
Copyright (C) 2026 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#import "SceneDelegate.h"
#import "ViewController.h"
#import "SettingsView.h"
#import "Game.h"
#import "Globals.h"
#import "Alert.h"
#import "dlfcn.h"

@interface SceneDelegate ()

@end

@implementation SceneDelegate


- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)connectionOptions {
	// Use this method to optionally configure and attach the UIWindow `window` to the provided UIWindowScene `scene`.
	// If using a storyboard, the `window` property will automatically be initialized and attached to the scene.
	// This delegate does not imply the connecting scene or session are new (see `application:configurationForConnectingSceneSession` instead).
	UIWindowScene *windowScene = (UIWindowScene*)scene;

	self.window = [[UIWindow alloc] initWithWindowScene:windowScene];
	self.gamesController = [[GamesViewController alloc] init];
	self.settingsController = [[SettingsViewController alloc] init];
	self.gamesNavController = [[UINavigationController alloc] initWithRootViewController:self.gamesController];
	self.settingsNavController = [[UINavigationController alloc] initWithRootViewController:self.settingsController];
		
	self.gamesController.tabBarItem = [[UITabBarItem alloc] initWithTitle:@"Games" image:[UIImage imageNamed:@"gamecontroller.fill"] tag:0];
	
	self.settingsController.tabBarItem = [[UITabBarItem alloc] initWithTitle:@"Settings" image:[UIImage imageNamed:@"gear"] tag:1];
	
	self.tabNavController = [[UITabBarController alloc] init];
	if (@available(iOS 18.0, *)) {
		self.tabNavController.traitOverrides.horizontalSizeClass = UIUserInterfaceSizeClassCompact;
	}
	[self.tabNavController setViewControllers:@[self.gamesNavController, self.settingsNavController] animated:YES];
	
	self.window.rootViewController = self.tabNavController;
	[self.window makeKeyAndVisible];
}


- (void)sceneDidDisconnect:(UIScene *)scene {
	// Called as the scene is being released by the system.
	// This occurs shortly after the scene enters the background, or when its session is discarded.
	// Release any resources associated with this scene that can be re-created the next time the scene connects.
	// The scene may re-connect later, as its session was not necessarily discarded (see `application:didDiscardSceneSessions` instead).
}


- (void)sceneDidBecomeActive:(UIScene *)scene {
	// Called when the scene has moved from an inactive state to an active state.
	// Use this method to restart any tasks that were paused (or not yet started) when the scene was inactive.
}


- (void)sceneWillResignActive:(UIScene *)scene {
	// Called when the scene will move from an active state to an inactive state.
	// This may occur due to temporary interruptions (ex. an incoming phone call).
}


- (void)sceneWillEnterForeground:(UIScene *)scene {
	// Called as the scene transitions from the background to the foreground.
	// Use this method to undo the changes made on entering the background.
	[self.gamesController updateGameDirs];
}


- (void)sceneDidEnterBackground:(UIScene *)scene {
	// Called as the scene transitions from the foreground to the background.
	// Use this method to save data, release shared resources, and store enough scene-specific state information
	// to restore the scene back to its current state.
}

extern int (*Host_main)(int, char **, const char *, int, const char*);

- (void)runEngine:(__weak Game *)game {
	
	if (![game isLoadable])
	{
		if (![game signGame])
		{
			NSLog(@"Could not sign the game: %@", game.gameInfo->title);
			[Alert displayErrorAlertWithMessage:[NSString stringWithFormat:@"%@ is not loadable, provided certificate data may not be correct", game.gameInfo->title]];
		}
	}
	
	void *engineHandle = dlopen([NSBundle.mainBundle pathForResource:@"libxash" ofType:@"dylib"].UTF8String, RTLD_NOW);
	if (!engineHandle)
	{
		[Alert displayErrorAlertWithMessage:[NSString stringWithFormat:@"Failed to open engine: %s", dlerror()]];
		return;
	}
	
	Host_main = dlsym(engineHandle, "Host_Main");
	NSString *savedArgs = [NSUserDefaults.standardUserDefaults objectForKey:[game.gameDir stringByAppendingString:@" Launch Args"]];
	
	if (!savedArgs)
		savedArgs = @"-log -dev 2";
	
	if (![game.gameDir isEqualToString:@"valve"])
		savedArgs = [savedArgs stringByAppendingFormat:@" -game %@", game.gameDir];
	
	NSArray<NSString*> *args = [savedArgs componentsSeparatedByString:@" "];
	
	char **argv = calloc(args.count + 2, sizeof(char*));
	
	
	for (int i = 0; i < args.count; i++)
	{
		argv[i + 1] = strdup(args[i].UTF8String);
	}
	
	int argc = (int)args.count + 1;
	argv[argc] = 0;
	
	//this SHOULD deallocate memory used up by the launcher
	self.window.hidden = YES;
	self.window = nil;
	self.gamesNavController = nil;
	self.gamesController = nil;
	self.settingsNavController = nil;
	self.settingsController = nil;
	self.tabNavController = nil;
			
	[NSFileManager.defaultManager changeCurrentDirectoryPath:documentsDirctory.path];
				
	int ret = Host_main(argc, argv, DEFAULT_GAMEDIR, 0, "");
	NSLog(@"Engine returned: %d", ret);
	exit(ret);
}


@end
