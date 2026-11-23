/*
SceneDelegate.h - Delegate for managing app window lifecycle
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

#import <UIKit/UIKit.h>
#import "Game.h"
#import "ViewController.h"

@interface SceneDelegate : UIResponder <UIWindowSceneDelegate>

@property (strong, nonatomic) UIWindow * window;
@property (strong, nonatomic) GamesViewController * gamesController;
@property (nonatomic) UIViewController *settingsController;
@property (nonatomic) UINavigationController *settingsNavController;
@property (nonatomic) UITabBarController *tabNavController;
@property (nonatomic) UINavigationController *gamesNavController;

- (void)runEngine:(__weak Game *)game;

@end

