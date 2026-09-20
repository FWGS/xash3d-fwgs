/*
AppDelegate.m - do not include this file, only copy the header
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

#import "AppDelegate.h"
#import "ViewController.h"
#import "SettingsView.h"
#import "dlfcn.h"
#import "Globals.h"
#import "Alert.h"
#import <AppTrackingTransparency/AppTrackingTransparency.h>

NSURL *documentsDirctory;
NSString *libraryDirectory;

@implementation AppDelegate


- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
	// Override point for customization after application launch.
	documentsDirctory = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask][0];
	NSString *logsdir = [documentsDirctory.path stringByAppendingPathComponent:@"launcherlogs.log"];
	[NSFileManager.defaultManager createFileAtPath:logsdir contents:nil attributes:nil];
	//freopen([logsdir fileSystemRepresentation], "a+", stderr);
	//freopen([logsdir fileSystemRepresentation], "a+", stdout);
	
	[NSFileManager.defaultManager createDirectoryAtPath:[documentsDirctory.path stringByAppendingPathComponent:@"Dummy folder"] withIntermediateDirectories:YES attributes:nil error:nil];
	
	libraryDirectory = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES)[0];
	
	self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
	self.gamesController = [[GamesViewController alloc] init];
	self.settingsController = [[SettingsViewController alloc] init];
	self.gamesNavController = [[UINavigationController alloc] initWithRootViewController:self.gamesController];
	self.settingsNavController = [[UINavigationController alloc] initWithRootViewController:self.settingsController];
		
	self.gamesController.tabBarItem = [[UITabBarItem alloc] initWithTitle:@"Games" image:[UIImage systemImageNamed:@"gamecontroller.fill"] tag:0];
	
	self.settingsController.tabBarItem = [[UITabBarItem alloc] initWithTitle:@"Settings" image:[UIImage systemImageNamed:@"gear"] tag:1];
	
	self.tabNavController = [[UITabBarController alloc] init];
	[self.tabNavController setViewControllers:@[self.gamesNavController, self.settingsNavController] animated:YES];
	
	self.window.rootViewController = self.tabNavController;
	[self.window makeKeyAndVisible];
	
	if (@available(iOS 14, *))
	{
		//engine will retrieve the id later
		[ATTrackingManager requestTrackingAuthorizationWithCompletionHandler:^(ATTrackingManagerAuthorizationStatus status){}];
	}
	
	return YES;
}

int (*Host_main)(int, char **, const char *, int, const char*);

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
	exit(0);
}


#if 0
#pragma mark - UISceneSession lifecycle


- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)connectingSceneSession options:(UISceneConnectionOptions *)options {
	// Called when a new scene session is being created.
	// Use this method to select a configuration to create the new scene with.
	return [[UISceneConfiguration alloc] initWithName:@"Default Configuration" sessionRole:connectingSceneSession.role];
}


- (void)application:(UIApplication *)application didDiscardSceneSessions:(NSSet<UISceneSession *> *)sceneSessions {
	// Called when the user discards a scene session.
	// If any sessions were discarded while the application was not running, this will be called shortly after application:didFinishLaunchingWithOptions.
	// Use this method to release any resources that were specific to the discarded scenes, as they will not return.
}

#endif
@end
