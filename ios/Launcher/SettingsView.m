/*
SettingsView.m - do not include this file, only copy the header
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

#import "SettingsView.h"
#import "Signing.h"
#import "PickerDelegate.h"
#import "TableViewTextCell.h"
#import "Globals.h"

@implementation SettingsViewController

- (void)viewDidLoad {
	[super viewDidLoad];
	[self initUI];
	[self constraintsInit];
}

- (void)initUI {
	self.title = @"Settings";
	if (![NSUserDefaults.standardUserDefaults URLForKey:@"P12 URL"])
	{
		//sideloading software may drop the necessary certificate in the app bundle
		NSURL *defaultp12 = [NSBundle.mainBundle URLForResource:@"ALTCertificate" withExtension:@"p12"];
		if (defaultp12)
			[NSUserDefaults.standardUserDefaults setObject:defaultp12.path forKey:@"P12 PATH"];
	}
	
	UITableViewHeaderFooterView *header = [[UITableViewHeaderFooterView alloc] init];
	header.textLabel.text = @"Signing";
	
	if (@available(iOS 13.0, *)) {
		self.settingsView = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStyleInsetGrouped];
	} else {
		self.settingsView = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStylePlain];
	}
	
	self.settingsView.translatesAutoresizingMaskIntoConstraints = NO;
	self.settingsView.tableHeaderView = header;
	self.settingsView.allowsMultipleSelection = NO;
	self.settingsView.keyboardDismissMode = UIScrollViewKeyboardDismissModeOnDrag;
	[self.settingsView registerClass:[UITableViewCell class] forCellReuseIdentifier:@"Settings cell"];
	self.settingsView.dataSource = self;
	self.settingsView.delegate = self;
	
	[self.view addSubview:self.settingsView];
	
	UITableViewCell *p12cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:@"Cert cell"];
	p12cell.textLabel.text = @"P12 file";
	p12cell.detailTextLabel.text = [NSUserDefaults.standardUserDefaults URLForKey:@"P12 PATH"].lastPathComponent;
	
	UITableViewCell *provisioncell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:@"Provision cell"];
	provisioncell.textLabel.text = @"Provision";
	provisioncell.detailTextLabel.text = [NSUserDefaults.standardUserDefaults URLForKey:@"PROVISION PATH"].lastPathComponent;
	
	TableViewTextCell *passwordcell = [[TableViewTextCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:@"Password cell"];
	passwordcell.textLabel.text = @"Password";
	passwordcell.selectionStyle = UITableViewCellSelectionStyleNone;
	passwordcell.textField.placeholder = @"Enter p12 password";
	passwordcell.textField.text = [NSUserDefaults.standardUserDefaults objectForKey:@"P12 PASSWORD"];
	passwordcell.textField.secureTextEntry = YES;
	passwordcell.textField.delegate = self;
	
	self.cells = @[p12cell, provisioncell, passwordcell];
}

- (void)constraintsInit {
	[NSLayoutConstraint activateConstraints:@[
		[self.settingsView.topAnchor constraintEqualToAnchor:self.view.topAnchor],
		[self.settingsView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
		[self.settingsView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
		[self.settingsView.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
	]];
}

- (void)selectP12 {
	NSLog(@"Selecting p12");
	[NSUserDefaults.standardUserDefaults removeObjectForKey:@"P12 PATH"];
	UIDocumentPickerViewController *documentPicker = [[UIDocumentPickerViewController alloc] initWithDocumentTypes:@[@"com.rsa.pkcs-12"] inMode:UIDocumentPickerModeOpen];
	documentPicker.allowsMultipleSelection = NO;
	NSURL *selection;
	PickerDelegate *delegate = [[PickerDelegate alloc] initWithURL:&selection];
	documentPicker.delegate = delegate;
	
	[self presentViewController:documentPicker animated:YES completion:nil];
	
	@autoreleasepool {
		while (!selection) {
			[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate distantFuture]];
		}
	}
	
	//todo: change documentsdirctory to an NSURL
	NSString *relativePath = [selection.path stringByStandardizingPath];
	relativePath = [relativePath substringFromIndex:documentsDirctory.path.length + 1];
	[NSUserDefaults.standardUserDefaults setObject:relativePath forKey:@"P12 PATH"];
}

- (void)selectProvision {
	NSLog(@"Selecting mobileprovision");
	[NSUserDefaults.standardUserDefaults removeObjectForKey:@"PROVISION PATH"];
	UIDocumentPickerViewController *documentPicker = [[UIDocumentPickerViewController alloc] initWithDocumentTypes:@[@"com.apple.mobileprovision"] inMode:UIDocumentPickerModeOpen];
	documentPicker.allowsMultipleSelection = NO;
	NSURL *selection;
	PickerDelegate *delegate = [[PickerDelegate alloc] initWithURL:&selection];
	documentPicker.delegate = delegate;
		
	[self presentViewController:documentPicker animated:YES completion:nil];
	@autoreleasepool {
		while (!selection)
		{
			[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate distantFuture]];
		}
	}
	
	NSURL *documentsURL = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask][0];
	NSString *relativePath = [selection.path substringFromIndex:documentsURL.path.length];
	[NSUserDefaults.standardUserDefaults setObject:relativePath forKey:@"PROVISION URL"];
}

- (nonnull UITableViewCell *)tableView:(nonnull UITableView *)tableView cellForRowAtIndexPath:(nonnull NSIndexPath *)indexPath { 
	UITableViewCell *cell = self.cells[indexPath.row];
	
	return cell;
}

- (NSInteger)tableView:(nonnull UITableView *)tableView numberOfRowsInSection:(NSInteger)section { 
	return self.cells.count;
}

- (void)tableView:(nonnull UITableView *)tableView didSelectRowAtIndexPath:(nonnull NSIndexPath *)indexPath {
	[tableView deselectRowAtIndexPath:indexPath animated:YES];
	UITableViewCell *cell = [tableView cellForRowAtIndexPath:indexPath];
	switch (indexPath.row) {
		case 0:
			[self selectP12];
			cell.detailTextLabel.text = [NSUserDefaults.standardUserDefaults URLForKey:@"P12 PATH"].lastPathComponent;
			break;
		case 1:
			[self selectProvision];
			cell.detailTextLabel.text = [NSUserDefaults.standardUserDefaults URLForKey:@"PROVISION PATH"].lastPathComponent;
			break;
		default:
			break;
	}

	[tableView reloadData];
}

- (BOOL)textFieldShouldReturn:(UITextField *)textField {
	[textField resignFirstResponder];
	return YES;
}

- (void)textFieldDidEndEditing:(UITextField *)textField {
	[NSUserDefaults.standardUserDefaults setObject:textField.text forKey:@"P12 PASSWORD"];
}

@end
