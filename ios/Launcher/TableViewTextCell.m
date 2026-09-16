/*
TableViewTextCell.m - do not include this file, only copy the header
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

#import "TableViewTextCell.h"

@implementation TableViewTextCell

- (instancetype)initWithStyle:(UITableViewCellStyle)style reuseIdentifier:(NSString *)reuseIdentifier {
	self = [super initWithStyle:style reuseIdentifier:reuseIdentifier];
	self.selectionStyle = UITableViewCellSelectionStyleNone;
	
	self.textField = [[UITextField alloc] init];
	self.textField.translatesAutoresizingMaskIntoConstraints = NO;
	self.textField.textAlignment = NSTextAlignmentRight;
	self.textField.autocorrectionType = UITextAutocorrectionTypeNo;

	
	[self.contentView addSubview:self.textField];
	
	[NSLayoutConstraint activateConstraints:@[
		[self.textField.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:20],
		[self.textField.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-20],
		[self.textField.topAnchor constraintEqualToAnchor:self.textLabel.topAnchor],
		[self.textField.bottomAnchor constraintEqualToAnchor:self.textLabel.bottomAnchor],
	]];
	
	return self;
}

@end
