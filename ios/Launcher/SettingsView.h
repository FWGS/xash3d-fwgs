/*
SettingsView.h
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

@interface SettingsViewController : UIViewController <UITableViewDataSource,UITableViewDelegate,UITextFieldDelegate>

@property(nonatomic) UITableView *settingsView;
@property(nonatomic) NSArray *cells;

@end
