/*
Signing.mm - do not include this file, only copy the header
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

#import <Foundation/Foundation.h>
#import "Signing.h"
#import "common.h"
#import "macho.h"

@interface Signer ()

//cant have this in header because it will be included in non c++ files
@property ZSignAsset *zsa;

@end

@implementation Signer

- (instancetype)initWithCert:(nonnull NSURL*)cert withProvision:(nullable NSURL*)provision password:(nullable NSString *)password{
	self.certData = [NSData dataWithContentsOfURL:cert];
	
	if (provision)
		self.provisionData = [NSData dataWithContentsOfURL:provision];
	
	if (password)
		self.password = password;
	
	if (![self initZSignAssetSimple])
		return nil;
	
	return [super init];
}

-(BOOL)initZSignAssetSimple {
	self.zsa = new ZSignAsset();
	if (!self.zsa->InitSimple([self.certData bytes], (int)[self.certData length], self.provisionData ? [self.provisionData bytes] : nil, self.provisionData ? (int)[self.provisionData length] : 0, self.password ? [self.password UTF8String] : ""))
	{
		NSLog(@"Provided certificate data or password is invalid!");
		return NO;
	}
	
	return YES;
}

- (BOOL)signAtURL:(NSURL*)machoURL {
	ZMachO *macho = new ZMachO();
	if (!macho->Init([machoURL.path UTF8String])) {
		NSLog(@"Specified URL doesn't point to a valid macho binary");
		return NO;
	}
	
	string bundle = NSBundle.mainBundle.bundleIdentifier.UTF8String;
	if (!macho->Sign(self.zsa, true, bundle, "", "", ""))
	{
		NSLog(@"Failed to sign binary");
		return NO;
	}
	
	return YES;
}

- (BOOL)signAtPath:(NSString*)machoPath {
	ZMachO *macho = new ZMachO();
	if (!macho->Init([machoPath UTF8String])) {
		NSLog(@"Specified URL doesn't point to a valid macho binary");
		return NO;
	}
	
	string bundle = NSBundle.mainBundle.bundleIdentifier.UTF8String;
	if (!macho->Sign(self.zsa, true, bundle, "", "", ""))
	{
		NSLog(@"Failed to sign binary");
		return NO;
	}
	
	return YES;
}



@end
