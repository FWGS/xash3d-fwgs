/*
SceneDelegate.m - do not include this file, only copy the header
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

#import "SceneDelegate.h"
#import "ViewController.h"
#import "SettingsView.h"

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
	self.settingsNavController = [[UINavigationController alloc] initWithRootViewController:self.settingsController];
	
	//self.subViewControllers = [[NSArray alloc] initWithObjects:settings, nil];
	
	self.gamesController.tabBarItem = [[UITabBarItem alloc] initWithTitle:@"Games"
																	 image:[UIImage systemImageNamed:@"gamecontroller.fill"]
																	   tag:0];
	
	self.settingsController.tabBarItem = [[UITabBarItem alloc] initWithTitle:@"Settings"
																		image:[UIImage systemImageNamed:@"gear"]
																		  tag:1];
	
	self.navController = [[UITabBarController alloc] init];
	[self.navController setViewControllers:@[self.gamesController, self.settingsNavController] animated:YES];
	
	self.window.rootViewController = self.navController;
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
}


- (void)sceneDidEnterBackground:(UIScene *)scene {
	// Called as the scene transitions from the foreground to the background.
	// Use this method to save data, release shared resources, and store enough scene-specific state information
	// to restore the scene back to its current state.
}


@end
