/*
GameSettings.m - do not include this file, only copy the header
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

#import "GameSettings.h"
#import "Game.h"

@implementation GameSettings

-(instancetype)initWithGame:(Game*)game {
	self.game = game;
	return [super init];
}

-(void)viewDidLoad {
	[super viewDidLoad];
	
	[self initUI];
	[self initConstraints];
}

-(void)initUI {
	UITableViewHeaderFooterView *header = [[UITableViewHeaderFooterView alloc] init];
	header.textLabel.text = @"Game configuration";
	
	if (@available(iOS 13.0, *)) {
		self.settingsView = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStyleInsetGrouped];
	}
	else {
		self.settingsView = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStylePlain];
	}
	
	self.settingsView.translatesAutoresizingMaskIntoConstraints = NO;
	self.settingsView.tableHeaderView = header;
	self.settingsView.allowsMultipleSelection = NO;
	self.settingsView.dataSource = self;
	self.settingsView.delegate = self;
	
	[self.view addSubview:self.settingsView];
	
	NSString *savedArgs = [NSUserDefaults.standardUserDefaults objectForKey:[self.game.gameDir stringByAppendingString:@" Launch Args"]];
	
	if (!savedArgs)
		savedArgs = @"-log -dev 2";
	
	TableViewTextCell *argsCell = [TableViewTextCell new];
	argsCell.textLabel.text = @"Launch Options";
	argsCell.selectionStyle = UITableViewCellSelectionStyleNone;
	argsCell.textField.placeholder = @"Enter Launch Options Here";
	argsCell.textField.text = savedArgs;
	argsCell.textField.delegate = self;
	
	self.cells = @[argsCell];
}

-(void)initConstraints {
	[NSLayoutConstraint activateConstraints:@[
		[self.settingsView.topAnchor constraintEqualToAnchor:self.view.topAnchor],
		[self.settingsView.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
		[self.settingsView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
		[self.settingsView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
	]];
}

-(UITableViewCell*)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
	return self.cells[indexPath.row];
}

-(NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
	return self.cells.count;
}

-(void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath{
	[tableView deselectRowAtIndexPath:indexPath animated:YES];
}

- (BOOL)textFieldShouldReturn:(UITextField *)textField {
	[textField resignFirstResponder];
	return YES;
}

-(void)textFieldDidEndEditing:(UITextField *)textField {
	[NSUserDefaults.standardUserDefaults setObject:textField.text forKey:[self.game.gameDir stringByAppendingString:@" Launch Args"]];
}
@end
