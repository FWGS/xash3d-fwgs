/*
AppDelegate.h - do not include this file, only copy the header
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

#import <UIKit/UIKit.h>
#import "Game.h"

@interface AppDelegate : UIResponder <UIApplicationDelegate>

@property (strong, nonatomic) UIWindow *window;
@property (nonatomic) UINavigationController *settingsNavController;
@property (nonatomic) UINavigationController *gamesNavController;
@property (strong, nonatomic) UIViewController * gamesController;
@property (nonatomic) UIViewController *settingsController;
@property (nonatomic) UITabBarController *tabNavController;

-(void)runEngine:(__weak Game *)game;

@end

