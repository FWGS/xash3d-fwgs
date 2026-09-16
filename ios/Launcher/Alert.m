/*
Alert.m - do not include this file, only copy the header
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

#import "Alert.h"
#import "AppDelegate.h"

NS_ASSUME_NONNULL_BEGIN

NSMutableArray<NSString *> *alertQueue;
BOOL showingAlert = NO;

@implementation Alert

+(void)displayErrorAlertWithMessage:(NSString *)message {
	if (!alertQueue)
		alertQueue = [[NSMutableArray alloc] initWithCapacity:2];

	if (showingAlert)
	{
		[alertQueue addObject:message];
		return;
	}
	
	UIViewController *vc = [[UIViewController alloc] init];
	UIWindow *alertWindow = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
	alertWindow.windowLevel = UIWindowLevelAlert + 1.0f;
	alertWindow.rootViewController = vc;
	[alertWindow makeKeyAndVisible];
	
	UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Error" message:message preferredStyle:UIAlertControllerStyleAlert];
	UIAlertAction *action = [UIAlertAction actionWithTitle:@"Ok" style:UIAlertActionStyleCancel handler:^(UIAlertAction *action){
		showingAlert = NO;
		alertWindow.hidden = YES;
		[self displayNextAlert];
	}];
	
	[alert addAction:action];
	[vc presentViewController:alert animated:YES completion:nil];
	
	showingAlert = YES;
}

+(void)displayNextAlert {
	NSString *message;
	
	if (alertQueue.count <= 0)
		return;
	
	message = [alertQueue objectAtIndex:0];
	[alertQueue removeObjectAtIndex:0];
	[self displayErrorAlertWithMessage:message];
}

@end

NS_ASSUME_NONNULL_END
